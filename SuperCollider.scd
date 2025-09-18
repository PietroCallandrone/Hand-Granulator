// ===================================
// SuperCollider Granular System
// Autostart version for terminal
// ===================================

//s.freeAll;

// Boot the server
s.waitForBoot{
	var port;
	 ~mixBus = Bus.audio(s, 2);  // stereo bus for all grains
	 ~grainSynths = IdentityDictionary.new;  // ← keep note→synth
    // -----------------------------------
    // VARIABLES INITIALIZATION
    // -----------------------------------

	~grainDur = 0.02;
    ~grainPos = 0.01;
    ~cutoff = 3000.0;
    ~res = 0.5;
    ~bpm = 120;
	~density = 0.8;
	~reverse = 0.0;
	~pitchShift = 1.0;
	~atk = 0.01;   // attack in seconds
    ~dec = 0.10;   // decay in seconds
    ~sus = 0.85;   // sustain level (0–1)
    ~rel = 0.20;   // release in seconds

    // -----------------------------------
    // SYNTH DEF
    // -----------------------------------
    SynthDef(\granularSynth, {

		arg grainDur = 0.02, grainPos = 2.0, gate = 1,
		cutoff = 3000.0, res = 0.5,
		bpm = 120, density = 1.0,
		bufnum = 0,
		pitchRatio = 1.0, pitchShift = 1.0,
		pitchWheel = 0.0,
		vel = 1.0, //Velocity for the midi
		atk=0.01, dec= 0.1, sus= 0.85, rel=0.2;

        var trig, grains, env, filtered;
		var trigRate, playbackRate, wheelSemis, wheelFactor;
		var shiftFactor = 2 ** (pitchShift / 12);


		//+-2 semitones for pitch wheel
		 wheelSemis  = pitchWheel * 2;
         wheelFactor = 2 ** ( wheelSemis / 12 );

		//──────────────────────────────────────
		// *** Grain-density → trigger rate ***
		//──────────────────────────────────────
		// CHOOSE ONE:
		// (a) absolute grains-per-second  … density =  1,  8, 150, …
		// trigRate = density.max(0.1);

		// (b) tempo-locked multiplier     … density = ½, 1, 4, 16, …
		trigRate = ((bpm / 60) * 4 * density).max(0.1);
		//          ^16th-note grid   ^×density  ^never drop to 0

        // Trigger
        trig = Impulse.kr(trigRate);

		/*-----------------------------------------
		②  pitchShift is an extra coarse detune
		-----------------------------------------*/
		playbackRate = BufRateScale.kr(bufnum)
                   * pitchRatio
                   * shiftFactor
                   * wheelFactor;

        // Grain buffer
        grains = GrainBuf.ar(2, trig, grainDur, bufnum, playbackRate, grainPos, 2, 0, -1, 512);

        // Filtering
        filtered = RLPF.ar(grains, cutoff, res.clip(0.01, 0.99));

        env = EnvGen.kr( Env.adsr(atk, dec, sus, rel), gate, doneAction: 2);

        Out.ar(~mixBus, filtered * env * vel*2);
    }).add;

	 SynthDef(\masterFader, { |amp=1.0|
        var mix = In.ar(~mixBus, 2);
        mix = Limiter.ar(mix * amp, 0.9, 0.05);
        Out.ar(0, mix);
    }).add;

	s.sync;

    // -----------------------------------
    // OSC COMMUNICATION SETUP
    // -----------------------------------
	port = 57121;
	thisProcess.openUDPPort(port);
    "✅ Listening for OSC on port %".format(port).postln;

    OSCFunc(
        { |msg, time, addr, recvPort|
            "📩 Received on port %: %".format(recvPort, msg).postln;
        },
        '/fromJUCE',
        nil,
        port
    );

   OSCdef(\startGrain, { |msg|
        var note = msg[1], vel = msg[2].clip(0,1);
        var freq = note.midicps;
        var pr   = freq/440;

		var bnum = (~buf != nil and: { ~buf.bufnum.notNil })
		.if({ ~buf.bufnum }, { 0 });


        var synth = Synth(\granularSynth, [
            \grainDur,   ~grainDur,
            \grainPos,   ~grainPos,
            \cutoff,     ~cutoff,
            \res,        ~res,
            \bpm,        ~bpm,
            \bufnum,     bnum,
            \pitchRatio, pr,
			\density, ~density,
			\pitchShift, ~pitchShift,
			\vel, vel,
			\atk, ~atk,
			\dec, ~dec,
			\sus, ~sus,
			\rel, ~rel
        ]);

        ~grainSynths[note] = synth; // store by note
		("▶️  Note "++note++" → synth "++synth.nodeID ++ "bpm:" ++~bpm).postln;
    }, '/start');

    // NOTE OFF → free only that note’s synth
   // on Note-Off, drive gate→0 to trigger your ASR’s release
OSCdef(\stopGrain, { |msg|
    var note = msg[1];
    if (note.notNil and: { ~grainSynths.includesKey(note) }) {
        var synth = ~grainSynths[note];
        synth.set(\gate, 0);               // tell the EnvGen to go into its release
        ~grainSynths.removeAt(note);       // drop it from our dictionary
        ("🎐 Releasing note " ++ note).postln;
    } {
        "⚠️  No active synth for note ".postln;
    }
}, '/stop');

	OSCdef(\env, { |msg|
  // msg: ['/env', atk, dec, sus, rel]
  ~atk = msg[1]; ~dec = msg[2]; ~sus = msg[3]; ~rel = msg[4];
  ~grainSynths.values.do { |synth|
    synth.set(\atk, ~atk, \dec, ~dec, \sus, ~sus, \rel, ~rel);
		synth.set(\gate, 0);
        synth.set(\gate, 1);
  };
  ("🎛 ADSR set → atk=%. dec=%. sus=%. rel=%.").format(~atk,~dec,~sus,~rel).postln;
}, '/env');

OSCdef(\grainUpdate, { |msg|
    ~grainDur = msg[1];
    ~grainPos = msg[2];
    ~cutoff = msg[3];
    ~density = msg[4];
    ~pitchShift = msg[5];

    ~grainSynths.values.do { |synth|
        synth.set(
            \grainDur,   ~grainDur,
            \grainPos,   ~grainPos,
            \cutoff,     ~cutoff,
			\density,    ~density,
			\pitchShift, ~pitchShift,

        );
    };

    ("🎛 Updated grain: dur=%. pos=%. cutoff=%. density=%. pitch=%."
        .format(~grainDur, ~grainPos, ~cutoff, ~density, ~pitchShift )).postln;

}, '/grain');


    OSCdef(\loadSample, { |msg|
        var path = msg[1].asString;
        if(path.notNil and:{ File.exists(path) }) {
            if(~buf.notNil) { ~buf.free; };
            "📂 Loading sample: %".format(path).postln;
            Buffer.readChannel(s, path, channels:[0], action:{|b|
                ~buf = b;
                "✅ Loaded buffer % (% frames)".format(b.bufnum, b.numFrames).postln;
                if(~grainSynth.notNil){
                    ~grainSynth.set(\bufnum, b.bufnum);
                    "🔄 Updated synth buffer to %".format(b.bufnum).postln;
                };
            });
        } {
            "❌ /loadSample: file not found → %".format(path).postln;
        };
    }, '/loadSample');

	OSCdef(\juceDisconnect, { |msg, time, addr, recvPort|
    ("🔌 JUCE has disconnected at " ++ time).postln;
		// s.quit; -> don't understand if it really works or not
}, '/disconnect');


	OSCdef(\setBPM, { |msg, time, addr, recvPort|
		var newBPM = msg[1].max(1);        // don’t let BPM drop below 1
		~bpm = newBPM;                     // update our global
		// push into every running grain-synth:
		~grainSynths.values.do { |s|
			s.set(\bpm, ~bpm);
		};
		("🎚️  BPM updated → " ++ ~bpm).postln;
	}, '/bpm', nil, port);

	OSCdef(\pitchWheel, { |msg|
    var bend = msg[1];  // expecting -1.0 … +1.0
    ~grainSynths.values.do { |synth|
        synth.set(\pitchWheel, bend);
    };
    ("🎚️ Pitch-wheel: " ++ bend).postln;
}, '/pitchWheel', nil, port);

	// Fixed LFO system for the Granular Synth
// Add this to your existing code or replace the current LFO implementation

// Modify the LFO system to allow rate changes without interruption

// Initialize default values for LFO system
~defaultShape = \sine;
~lastLfoParam = nil;
~lastMin = 0.0;
~lastMax = 1.0;
~lastRate = nil;
~lastShape = ~defaultShape;
~lfoRoutine = nil;
~lfoCurrentRate = nil; // Store the current rate separately

// Parameter name mapping and normalization remain the same
~paramNameMap = Dictionary[
    \graincutoff -> \cutoff,
    \graindur -> \grainDur,
    \grainpos -> \grainPos,
    \graindensity -> \density,
    \grainpitch -> \pitchShift,
];

// Function to normalize parameter names
~normalizeParamName = { |rawName|
    var normalized;
    rawName = rawName.asString.toLower.asSymbol;
    normalized = ~paramNameMap[rawName];
    if(normalized.isNil) {
        ("⚠️ Unknown parameter name: % - using as-is").format(rawName).postln;
        normalized = rawName;
    };
    normalized;
};

// Pre-define ranges for various parameters
~lfoRanges = Dictionary[
    \grainDur -> [0.005, 0.5],     // 5ms to 500ms
    \grainPos -> [0.1, 4.0],      // Position in buffer
    \cutoff -> [50.0, 1000.0],   // Filter cutoff
    \res -> [0.1, 0.9],           // Filter resonance
    \density -> [0.005, 5.0],     // Grain density multiplier
    \pitchShift -> [-12.0, 12.0]  // Pitch shift in semitones
];

// LFO shape functions
~lfoShapes = Dictionary[
    \sine -> { |ph| sin(ph * 2pi) },
    \triangle -> { |ph| (2 * (2 * ph % 2) - 1).abs * 2 - 1 },
    \saw -> { |ph| (ph * 2 - 1) },
    \square -> { |ph| (ph < 0.5).if(1, -1) }
];

// Stop any running LFO
~stopLFO = {
    if(~lfoRoutine.notNil) {
        ~lfoRoutine.stop;
        ~lfoRoutine = nil;
        ~lfoCurrentRate = nil;
        "🛑 LFO stopped.".postln;
    };
};

// Modified start LFO function that uses a rate variable
~startLFO = { |param, min, max, rate, shape=\sine|
    // safety checks
    if(~grainSynths.isEmpty) {
        "⚠️ No active grain synths – start a note first.".postln;
        ^nil
    };

    if(~lfoShapes[shape].isNil) {
        "⚠️ Unknown shape %, defaulting to sine.".format(shape).postln;
        shape = \sine;
    };

    // Store current settings
    ~lastLfoParam = param;
    ~lastMin = min;
    ~lastMax = max;
    ~lastRate = rate;
    ~lastShape = shape;

    // Only stop and restart if there's no active LFO or if changing parameters other than rate
    if(~lfoRoutine.isNil) {
        ("🌊 LFO started → param: %, rate: % Hz, shape: %")
            .format(param, rate, shape).postln;

        // Initialize the current rate
        ~lfoCurrentRate = rate;

        ~lfoRoutine = Routine({
            var phase = 0;
            var controlPeriod = 0.0005; // 20ms control rate (50Hz)

            loop {
                var norm = ~lfoShapes[shape].(phase);  // -1 .. +1
                var value = norm.linlin(-1, 1, min, max); // map to range

                ~grainSynths.values.do { |syn|
                    syn.set(param, value);
                };

                controlPeriod.wait;

                // Use the current rate value (which can be changed externally)
                // This allows rate updates without restarting the routine
                phase = (phase + (~lfoCurrentRate * controlPeriod)) % 1.0;
            };
        }).play;
    } {
        // If already running, just update the rate
        if(~lastLfoParam != param || ~lastShape != shape || ~lastMin != min || ~lastMax != max) {
            // If changing parameters other than rate, need to restart
            ~stopLFO.value;
            ~startLFO.(param, min, max, rate, shape);
        } {
            // Only update the rate (no restart)
            ~lfoCurrentRate = rate;
            ("🔄 LFO rate updated → % Hz").format(rate).postln;
        }
    };
};

// Updated OSC handlers to use the new system

// LFO Target still stops/starts because target is changing
OSCdef(\lfoTarget, { |msg|
    var rawParam = msg[1];
    var param = ~normalizeParamName.(rawParam);
    var paramRange;

    ("🔄 Parameter name converted: % → %").format(rawParam, param).postln;

    paramRange = ~lfoRanges[param];
    if(paramRange.isNil) {
        ("⚠️ No range defined for parameter %, using default range 0-1").format(param).postln;
        paramRange = [0.0, 1.0];
    };

    // Store parameter and range
    ~lastLfoParam = param;
    ~lastMin = paramRange[0];
    ~lastMax = paramRange[1];

    ("🎯 LFO target set → param: % (range: % to %)")
        .format(param, ~lastMin, ~lastMax).postln;

    // If rate is already set, start LFO immediately
    if(~lastRate.notNil && ~lastRate != 0) {
        ~stopLFO.value;  // Stop existing LFO since we're changing parameter
        ~startLFO.(param, ~lastMin, ~lastMax, ~lastRate, ~lastShape);
    } {
        "⏱️ Waiting for rate to start LFO".postln;
    };
}, '/lfoTarget');

// LFO Rate - now updates the rate continuously if possible
OSCdef(\lfoRate, { |msg|
    var newRate = msg[1].asFloat;
    ~lastRate = newRate;

    // If target parameter is set, start/update LFO
    if(~lastLfoParam.notNil) {
        if(~lfoRoutine.isNil) {
            // Not running, start fresh
            ~startLFO.(~lastLfoParam, ~lastMin, ~lastMax, newRate, ~lastShape);
        } {
            // Already running, just update the rate
            ~lfoCurrentRate = newRate;
            ("🔄 LFO rate updated → % Hz").format(newRate).postln;
        }
    } {
        ("⚠️ Rate set to % Hz but no target parameter defined yet")
            .format(newRate).postln;
    };
}, '/lfoRate');

// Stop LFO via OSC (unchanged)
OSCdef(\lfoStop, { |msg|
    ~stopLFO.value;
}, '/lfoStop');

	  s.sync; //wait for the previous part ot be completed
    "🎵 SuperCollider Granular System ready!".postln;
	~masterFader = Synth(\masterFader, [\amp, 4.0]);


};


