import oscP5.*;
import netP5.*;

OscP5       osc;
PVector[][] hand       = new PVector[2][21];
long[]      lastHandMs = new long[2];
int         timeoutMs  = 100;

//glow params
final float BASE_SIZE   = 10; //Dots thickness
final float GLOW_SCALE  = 1.0;
final int   GLOW_BLUR   = 10;
final float GLOW_ALPHA  = 100;
PGraphics meshLayer, palmMask;
// fixed thickness baseline for the fingers
final float LINE_WEIGHT = 13;

//manual hand-curve offsets (no longer used for straight lines)
final float[] PALM_OFFSETS = {PI/7f, -PI/7f};
final float[] WEB_OFFSETS  = {-PI/4f, PI/4f};

//half-res glow buffer
PGraphics  glowLayer;
int        glowW, glowH;

//Boolean of the fingers
boolean IndexFinger = false;
boolean MiddleFinger = false;
boolean PinkyFinger = false;
boolean RingFinger = false;
boolean IndexLeftFinger = false;
boolean MiddleLeftFinger = false;
boolean IndexRightFinger = false;
boolean MiddleRightFinger = false;
String currentPage = "synth";

// Dynamic background parameters
float[] bgNoiseX;  // Noise position X values
float[] bgNoiseY;  // Noise position Y values
int numBgPoints = 25;  // Number of background points
float noiseScale = 0.003;  // Scale of the noise
float noiseSpeed = 0.005;  // Speed of noise movement

//Particle system
ArrayList<Particle> particles;
int maxParticles = 100;
PVector gravity = new PVector(0, 0.05);

//Geometric pattern
float patternRotation = 0;
float patternSize = 200;

//Energy field visualization
PVector[][] flowField;
int flowFieldCols, flowFieldRows;
int flowFieldScale = 20;

//skeleton connections
final int[][] connections = {
  {0, 1}, {1, 2}, {2, 3}, {3, 4}, {2, 5},
 {5, 6}, {6, 7}, {7, 8},
 {9, 10}, {10, 11}, {11, 12},
 {13, 14}, {14, 15}, {15, 16},
  {0, 17}, {17, 18}, {18, 19}, {19, 20},
  {5, 9}, {9, 13}, {13, 17}
};

/* ── visual-parameter globals ─────────────────────────────────── */
float vGrainDur, vGrainPos, vCutoff,
  vGrainDensity, vGrainPitch,
  vGrainReverse, vLfoRate;

// Green hue range
final float GREEN_HUE_MIN = 80;
final float GREEN_HUE_MAX = 160;

// Sound reactive elements
float soundEnergy = 0;
float soundEnergySmooth = 0;
float smoothingFactor = 0.1;
/* ──────────────────────────────────────────────────────────────── */

void setup() {
  size(640, 480, P2D);
  frameRate(60);
  colorMode(HSB, 360, 255, 255, 255);
  smooth(4);

  osc = new OscP5(this, 9003); //Connect to the port
  for (int h = 0; h < 2; h++)
    for (int i = 0; i < 21; i++)
      hand[h][i] = new PVector(0.5, 0.5);

  glowW = width >> 1;
  glowH = height >> 1;
  glowLayer = createGraphics(glowW, glowH, P2D);
  glowLayer.noSmooth();
  glowLayer.colorMode(HSB, 360, 255, 255, 255);

  //Initialize background noise values
  bgNoiseX = new float[numBgPoints];
  bgNoiseY = new float[numBgPoints];
  for (int i = 0; i < numBgPoints; i++) {
    bgNoiseX[i] = random(1000);
    bgNoiseY[i] = random(1000);
  }

  //Initialize particles
  particles = new ArrayList<Particle>();

  //Initialize flow field
  flowFieldCols = width / flowFieldScale;
  flowFieldRows = height / flowFieldScale;
  flowField = new PVector[flowFieldCols][flowFieldRows];
  for (int i = 0; i < flowFieldCols; i++) {
    for (int j = 0; j < flowFieldRows; j++) {
      flowField[i][j] = new PVector(0, 0);
    }
  }
  meshLayer = createGraphics(width, height, P2D);
  palmMask  = createGraphics(width, height, P2D);
}

//line from thumb to index
void drawThumbIndexLink(int h, float hue, float weight) {
  PVector thumbJoint = hand[h][2];   //dot of the thumb
  PVector indexTip   = hand[h][5];   //dot of the index finger
  stroke(hue, 255, 255);
  strokeWeight(weight);
  line(
    thumbJoint.x * width, thumbJoint.y * height,
    indexTip.x   * width, indexTip.y   * height
    );
}

//Draw the thunder that goes from thumb to index
void drawThunderLink(int h, float colHue, float alpha, int segments, float maxOffset, float weight) {
  // get tip positions in pixel‐space
  PVector A = hand[h][4];   // thumb tip
  PVector B = hand[h][8];   // index tip
  float ax = A.x * width, ay = A.y * height;
  float bx = B.x * width, by = B.y * height;

  stroke(colHue, 255, 255, alpha);
  strokeWeight(weight);
  noFill();
  beginShape();
    // start at thumb
    vertex(ax, ay);

    //precompute perpendicular unit
    float dx = bx - ax;
    float dy = by - ay;
    float len = dist(ax, ay, bx, by);
    float nx = 0, ny = 0;
    if (len > 0) {
      nx = -dy / len;
      ny =  dx / len;
    }

    //intermediate jagged points
    for (int i = 1; i < segments; i++) {
      float t = i / (float)segments;
      float x = lerp(ax, bx, t);
      float y = lerp(ay, by, t);

      // small perpendicular kick
      float off = random(-maxOffset, maxOffset);
      x += nx * off;
      y += ny * off;

      vertex(x, y);
    }

    //end at index tip
    vertex(bx, by);
  endShape();
}

//Draw the thunder that goes from thumb to middle
void drawThunderLinkMIddle(int h, float colHue, float alpha, int segments, float maxOffset, float weight) {
  // get tip positions in pixel‐space
  PVector A = hand[h][4];   //thumb tip
  PVector B = hand[h][12];   //middle tip
  float ax = A.x * width, ay = A.y * height;
  float bx = B.x * width, by = B.y * height;

  stroke(colHue, 255, 255, alpha);
  strokeWeight(weight);
  noFill();
  beginShape();
    // start at thumb
    vertex(ax, ay);

    //precompute perpendicular unit
    float dx = bx - ax;
    float dy = by - ay;
    float len = dist(ax, ay, bx, by);
    float nx = 0, ny = 0;
    if (len > 0) {
      nx = -dy / len;
      ny =  dx / len;
    }

    //intermediate jagged points
    for (int i = 1; i < segments; i++) {
      float t = i / (float)segments;
      float x = lerp(ax, bx, t);
      float y = lerp(ay, by, t);

      // small perpendicular kick
      float off = random(-maxOffset, maxOffset);
      x += nx * off;
      y += ny * off;

      vertex(x, y);
    }

    //end at index tip
    vertex(bx, by);
  endShape();
}

//Draw the thunder that goes from thumb to ring
void drawThunderLinkRing(int h, float colHue, float alpha, int segments, float maxOffset, float weight) {
  // get tip positions in pixel‐space
  PVector A = hand[h][4];   //thumb tip
  PVector B = hand[h][16];   //ring tip
  float ax = A.x * width, ay = A.y * height;
  float bx = B.x * width, by = B.y * height;

  stroke(colHue, 255, 255, alpha);
  strokeWeight(weight);
  noFill();
  beginShape();
    // start at thumb
    vertex(ax, ay);

    // precompute perpendicular unit
    float dx = bx - ax;
    float dy = by - ay;
    float len = dist(ax, ay, bx, by);
    float nx = 0, ny = 0;
    if (len > 0) {
      nx = -dy / len;
      ny =  dx / len;
    }

    //intermediate jagged points
    for (int i = 1; i < segments; i++) {
      float t = i / (float)segments;
      float x = lerp(ax, bx, t);
      float y = lerp(ay, by, t);

      //small perpendicular kick
      float off = random(-maxOffset, maxOffset);
      x += nx * off;
      y += ny * off;

      vertex(x, y);
    }

    //end at index tip
    vertex(bx, by);
  endShape();
}

//Draw the thunder that goes from thumb to pink
void drawThunderLinkPinky(int h, float colHue, float alpha, int segments, float maxOffset, float weight) {
  // get tip positions in pixel‐space
  PVector A = hand[h][4];   // thumb tip
  PVector B = hand[h][20];   //pink tip
  float ax = A.x * width, ay = A.y * height;
  float bx = B.x * width, by = B.y * height;

  stroke(colHue, 255, 255, alpha);
  strokeWeight(weight);
  noFill();
  beginShape();
    // start at thumb
    vertex(ax, ay);

    //precompute perpendicular unit
    float dx = bx - ax;
    float dy = by - ay;
    float len = dist(ax, ay, bx, by);
    float nx = 0, ny = 0;
    if (len > 0) {
      nx = -dy / len;
      ny =  dx / len;
    }

    //intermediate jagged points
    for (int i = 1; i < segments; i++) {
      float t = i / (float)segments;
      float x = lerp(ax, bx, t);
      float y = lerp(ay, by, t);

      // small perpendicular kick
      float off = random(-maxOffset, maxOffset);
      x += nx * off;
      y += ny * off;

      vertex(x, y);
    }

    //end at index tip
    vertex(bx, by);
  endShape();
}

//rewritten to draw into whatever PGraphics you pass in
void drawPalmMesh(PGraphics pg, PVector[] pts, float w, float h) {
  int[] B = {0,2,5,9,13,17};
  int rows = 8, cols = 8;

  // U‐lines
  for(int r=0; r<rows; r++){
    float t = r/float(rows-1);
    PVector L = PVector.lerp(pts[B[0]], pts[B[2]], t),
            R = PVector.lerp(pts[B[4]], pts[B[5]], t);
    pg.beginShape();
      for(int i=0; i<=20; i++){
        float s = i/20f;
        PVector p = PVector.lerp(L, R, s);

        // jitter
        PVector dir  = PVector.sub(R,L).normalize();
        PVector norm = new PVector(-dir.y, dir.x);
        float  n     = noise(p.x*10, p.y*10, frameCount*0.02f);
        p.add(PVector.mult(norm, map(n,0,1,-20,20)/rows));

        pg.vertex(p.x*w, p.y*h);
      }
    pg.endShape();
  }

  // V‐lines
  for(int c=0; c<cols; c++){
    float t = c/float(cols-1);
    PVector Bp = PVector.lerp(pts[B[0]], pts[B[1]], t),
            Tp = PVector.lerp(pts[B[2]], pts[B[3]], t);
    pg.beginShape();
      for(int i=0; i<=20; i++){
        float s = i/20f;
        PVector p = PVector.lerp(Bp, Tp, s);

        PVector dir  = PVector.sub(Tp,Bp).normalize();
        PVector norm = new PVector(-dir.y, dir.x);
        float  n     = noise(p.x*10, p.y*10, frameCount*0.02f+100);
        p.add(PVector.mult(norm, map(n,0,1,-20,20)/cols));

        pg.vertex(p.x*w, p.y*h);
      }
    pg.endShape();
  }

  //random chords for the inline thunders
  for(int i=0; i<15; i++){
    float u1=random(1), v1=random(1), u2=random(1), v2=random(1);
    PVector A = PVector.lerp(
                  PVector.lerp(pts[B[0]],pts[B[2]],u1),
                  PVector.lerp(pts[B[4]],pts[B[5]],u1),
                  v1),
            C = PVector.lerp(
                  PVector.lerp(pts[B[0]],pts[B[2]],u2),
                  PVector.lerp(pts[B[4]],pts[B[5]],u2),
                  v2);
    pg.strokeWeight(0.5);
    pg.line(A.x*w,A.y*h, C.x*w,C.y*h);
  }
}


void draw() {
  //Clear with transparent black for trails
  background(0, 0, 0, 20);

  long now = millis();
  boolean anyFresh = (now - lastHandMs[0] <= timeoutMs) ||
    (now - lastHandMs[1] <= timeoutMs);

  //derive dynamic visual factors from synth values
  float hueShift = GREEN_HUE_MIN + (vGrainPos * (GREEN_HUE_MAX - GREEN_HUE_MIN)) % (GREEN_HUE_MAX - GREEN_HUE_MIN);
  float szMul       = 1 + vGrainDur * 5;
  float dynGlowAlph = constrain(map(vGrainDensity, 1, 20, 40, 180), 40, 255);
  float dynWeight   = LINE_WEIGHT + vGrainPitch * 2;
  boolean reversing = vGrainReverse > 0.5;

  //Update sound energy parameters
  soundEnergy = vGrainDensity * 5;
  soundEnergySmooth = lerp(soundEnergySmooth, soundEnergy, smoothingFactor);

  //Draw dynamic background
  drawDynamicBackground(hueShift);

  //Update and draw the flow field
  updateFlowField(hueShift);
  drawFlowField();

  //Draw geometric pattern
  drawGeometricPattern(hueShift);

  if (anyFresh) {
    // glow pass
    glowLayer.beginDraw();
    glowLayer.clear();
    glowLayer.noStroke();
    glowLayer.fill(hueShift, 140, 255, dynGlowAlph);
    float sz = BASE_SIZE * GLOW_SCALE * szMul;
    for (int h = 0; h < 2; h++) {
      if (now - lastHandMs[h] <= timeoutMs) {
        for (int i = 0; i < 21; i++) {
          PVector p = hand[h][i];
          glowLayer.ellipse(p.x * glowW, p.y * glowH, sz, sz);
        }
      }
    }
    glowLayer.stroke(hueShift, 140, 255, dynGlowAlph);
    glowLayer.strokeWeight(dynWeight * 1.5);
    int[][] fingers = {
      {5, 6, 7, 8}, //index (no palm link)
      {0, 1, 2, 3, 4}, //thumb for skeleton curves
      {0, 9, 10, 11, 12}, //middle
      {0, 13, 14, 15, 16}, //ring
      {0, 17, 18, 19, 20}    //pinky
    };
    for (int h = 0; h < 2; h++) {
      if (now - lastHandMs[h] <= timeoutMs) {
        // custom thumb→index glow line
        PVector t = hand[h][3];    // thumb IP
        PVector i2 = hand[h][6];   // index PIP
        glowLayer.line(t.x*glowW, t.y*glowH, i2.x*glowW, i2.y*glowH);
        // finger “bones” glow
        for (int[] finger : fingers) {
          for (int k = 0; k < finger.length - 1; k++) {
            PVector a = hand[h][finger[k]];
            PVector b = hand[h][finger[k+1]];
            glowLayer.line(a.x*glowW, a.y*glowH, b.x*glowW, b.y*glowH);
          }
        }
      }
    }

    glowLayer.filter(BLUR, GLOW_BLUR);
    glowLayer.endDraw();

    blendMode(ADD);
    image(glowLayer, 0, 0, width, height);
    blendMode(BLEND);

    //crisp dots
    noStroke();
    fill(hueShift, 216, reversing ? 120 : 230);
    for (int h = 0; h < 2; h++) {
      if (now - lastHandMs[h] <= timeoutMs) {
        drawThumbIndexLink(h, hueShift, dynWeight); //draw the line from thumb to index
        drawJointCircles(h, hueShift); //Draw the light blue dots on the fingers
        //Logic to draw the thunders between fingers and thumb
        if (currentPage.equals("synth")) {
          if(IndexFinger==true && h==1)
          {
            drawThunderLink(h, 220, 120, 6, 12, 4);
          }
          if(MiddleFinger==true  && h==1)
          {
            drawThunderLinkMIddle(h, 220, 120, 6, 12, 4);
          }
          if(RingFinger==true  && h==1)
          {
            drawThunderLinkRing(h, 220, 120, 6, 12, 4);
          }
         if(PinkyFinger==true  && h==1)
          {
            drawThunderLinkPinky(h, 220, 120, 6, 12, 4);
          }
        }
        if (currentPage.equals("drum")) {
          if(IndexLeftFinger==true  && h==0)
          {
            drawThunderLink(h, 220, 120, 6, 12, 4);
          }
          if(MiddleLeftFinger==true  && h==0)
          {
            drawThunderLinkMIddle(h, 220, 120, 6, 12, 4);
          }
           if(IndexRightFinger==true  && h==1)
          {
            drawThunderLink(h, 220, 120, 6, 12, 4);
          }
           if(MiddleRightFinger==true  && h==1)
          {
            drawThunderLinkMIddle(h, 220, 120, 6, 12, 4);
          }
      }
    }
    }

    //draw soft, curved hand skeleton
    for (int h = 0; h < 2; h++) {
      if (now - lastHandMs[h] <= timeoutMs) {
        drawSoftHand(h, hueShift, dynWeight);
      }
    }

    //Add particles from hand points
    for (int h = 0; h < 2; h++) {
      if (now - lastHandMs[h] <= timeoutMs && random(1) < 0.3) {
        int fingerIndex = int(random(5)) + 16;
        PVector source = hand[h][fingerIndex];
        addParticles(source.x * width, source.y * height, 3, hueShift);
      }
    }

    //Update and draw particles
    updateAndDrawParticles();
  }
}

void drawDynamicBackground(float baseHue) {
  for (int i = 0; i < numBgPoints; i++) {
    bgNoiseX[i] += noiseSpeed;
    bgNoiseY[i] += noiseSpeed * 0.5;
  }
  blendMode(ADD);
  noStroke();
  for (int i = 0; i < numBgPoints; i++) {
    float nx = noise(bgNoiseX[i]) * width;
    float ny = noise(bgNoiseY[i]) * height;
    float pointHue = baseHue + sin(i * 0.1 + frameCount * 0.01) * 15;
    pointHue = constrain(pointHue, GREEN_HUE_MIN, GREEN_HUE_MAX);
    float size = 100 + vGrainDur * 100 + sin(frameCount * 0.02 + i) * 50;
    fill(pointHue, 120, 120, 10);
    ellipse(nx, ny, size, size);
  }
  blendMode(BLEND);
}

void drawGeometricPattern(float baseHue) {
  pushMatrix();
  translate(width/2, height/2);
  patternRotation += vLfoRate * 0.01;
  rotate(patternRotation);
  float dynamicSize = patternSize * (1 + soundEnergySmooth * 0.05);
  noFill();
  strokeWeight(1);
  for (int i = 0; i < 6; i++) {
    float ringHue = baseHue - 15 + i * 5;
    ringHue = constrain(ringHue, GREEN_HUE_MIN, GREEN_HUE_MAX);
    stroke(ringHue, 200, 200, 50);
    pushMatrix();
    rotate(frameCount * 0.001 * (i+1) * (vGrainReverse > 0.5 ? -1 : 1));
    int sides = 5 + i;
    beginShape();
    for (int j = 0; j < sides; j++) {
      float angle = TWO_PI * j / sides;
      float rad = dynamicSize * (0.2 + i * 0.1);
      vertex(cos(angle) * rad, sin(angle) * rad);
    }
    endShape(CLOSE);
    popMatrix();
  }
  popMatrix();
}

void updateFlowField(float baseHue) {
  float t = frameCount * 0.01;
  for (int i = 0; i < flowFieldCols; i++) {
    for (int j = 0; j < flowFieldRows; j++) {
      float x = i * flowFieldScale;
      float y = j * flowFieldScale;
      float angle = noise(x * 0.01, y * 0.01, t) * TWO_PI * 4;
      flowField[i][j].x = cos(angle) * vGrainPitch;
      flowField[i][j].y = sin(angle) * vGrainPitch;
    }
  }
}

void drawFlowField() {
  stroke(GREEN_HUE_MIN + 20, 150, 200, 30);
  strokeWeight(0.8);
  for (int i = 0; i < flowFieldCols; i += 3) {
    for (int j = 0; j < flowFieldRows; j += 3) {
      float x = i * flowFieldScale;
      float y = j * flowFieldScale;
      pushMatrix();
      translate(x, y);
      line(0, 0, flowField[i][j].x * 10, flowField[i][j].y * 10);
      popMatrix();
    }
  }
}


void drawJointCircles(int h, float hue) {
  // the five fingertip landmark indices
  int[] tipIdx = { 4, 8, 12, 16, 20 };
  
stroke(200, 150, 200);
  strokeWeight(2);
  noFill();
  
  for (int idx : tipIdx) {
    PVector p = hand[h][idx];
    float x = p.x * width;
    float y = p.y * height;
    ellipse(x, y, BASE_SIZE * 1.5, BASE_SIZE * 1.5);
  }
}




// New: draw hand with filled palm and curved, tapered fingers
void drawSoftHand(int h, float baseHue, float dynWeight) {
  PVector[] pts = hand[h];
  // Filled palm
  int[] palmIdx = {0, 2, 5, 9, 13, 17}; //the green veil under the palm
  noStroke();
  fill(baseHue, 255, 200, 60);
  beginShape();
  for (int idx : palmIdx) {
    PVector p = pts[idx];
    vertex(p.x * width, p.y * height);
  }
  endShape(CLOSE);

  meshLayer.beginDraw();
  meshLayer.clear();
  meshLayer.stroke(baseHue, 255, 255);
  meshLayer.strokeWeight(1);
  meshLayer.noFill();  // this call writes into meshLayer because we’re inside beginDraw/endDraw
drawPalmMesh(meshLayer, pts, width, height);
  meshLayer.endDraw();
  // — build a white mask of exactly the palm shape —
  palmMask.beginDraw();
    palmMask.clear();
    palmMask.noStroke();
    palmMask.fill(255);
    palmMask.beginShape();
      for (int idx : palmIdx) {
        PVector p = pts[idx];
        palmMask.vertex(p.x * width, p.y * height);
      }
    palmMask.endShape(CLOSE);
  palmMask.endDraw();

  // — clip meshLayer to palmMask —
  meshLayer.mask(palmMask);

  // — draw the masked mesh back to main canvas —
  blendMode(BLEND);
  image(meshLayer, 0, 0);

  // Curved fingers
  int[][] fingers = {
    {0, 1, 2, 3, 4},
    {5, 6, 7, 8},
    {10, 11, 12},
    {13, 14, 15, 16},
    {0, 17, 18, 19, 20},
    { 2, 5, 9, 13, 17 }
  };
  for (int[] finger : fingers) {
    for (int k = 0; k < finger.length - 1; k++) {
      PVector a = pts[finger[k]];
      PVector b = pts[finger[k+1]];
      float t = k / float(finger.length - 2);
      stroke(baseHue, 255, 255);
      strokeWeight(LINE_WEIGHT);
      PVector mid = PVector.lerp(a, b, 0.5);
      PVector dir = PVector.sub(b, a);
      PVector normal = new PVector(-dir.y, dir.x).normalize().mult(dir.mag() * 0.1);
      bezier(
        a.x*width, a.y*height,
        mid.x*width + normal.x, mid.y*height + normal.y,
        mid.x*width - normal.x, mid.y*height - normal.y,
        b.x*width, b.y*height
        );
    }
  }
  //Drawing interlienar lines
  stroke(baseHue, 255, 255);
  strokeWeight(LINE_WEIGHT);  // tweak this multiplier for line-thickness
  for (int[] c : connections) {
    PVector a = pts[c[0]];
    PVector b = pts[c[1]];
    line(a.x * width, a.y * height,
      b.x * width, b.y * height);
  }
}

void addParticles(float x, float y, int count, float hue) {
  for (int i = 0; i < count; i++) {
    if (particles.size() < maxParticles) {
      particles.add(new Particle(x, y, hue));
    }
  }
}

void updateAndDrawParticles() {
  for (int i = particles.size() - 1; i >= 0; i--) {
    Particle p = particles.get(i);
    p.update();
    p.display();
    if (p.isDead()) {
      particles.remove(i);
    }
  }
}

class Particle {
  PVector position, velocity, acceleration;
  float lifespan, size, hue;

  Particle(float x, float y, float h) {
    position = new PVector(x, y);
    velocity = PVector.random2D().mult(random(1, 3));
    acceleration = new PVector(0, 0);
    lifespan = 255;
    size = random(3, 8);
    hue = constrain(h + random(-10, 10), GREEN_HUE_MIN, GREEN_HUE_MAX);
  }

  void update() {
    acceleration.add(gravity);
    int col = constrain(int(position.x / flowFieldScale), 0, flowFieldCols-1);
    int row = constrain(int(position.y / flowFieldScale), 0, flowFieldRows-1);
    PVector force = flowField[col][row].copy().mult(0.1);
    acceleration.add(force);
    velocity.add(acceleration);
    velocity.limit(4);
    position.add(velocity);
    acceleration.mult(0);
    lifespan -= 2.5;
  }

  void display() {
    noStroke();
    fill(hue, 180, 255, lifespan);
    ellipse(position.x, position.y, size, size);
  }

  boolean isDead() {
    return lifespan <= 0 ||
      position.x < 0 || position.x > width ||
      position.y < 0 || position.y > height;
  }
}

void oscEvent(OscMessage msg) {
  String addr = msg.addrPattern();
  if (addr.equals("/visParams")) {
    vGrainDur     = msg.get(0).floatValue();
    vGrainPos     = msg.get(1).floatValue();
    vCutoff       = msg.get(2).floatValue();
    vGrainDensity = msg.get(3).floatValue();
    vGrainPitch   = msg.get(4).floatValue();
    vGrainReverse = msg.get(5).floatValue();
    vLfoRate      = msg.get(6).floatValue();
    return;
  }
  if (addr.startsWith("/hand/")) {
    String[] parts = split(addr, '/');
    int h = int(parts[2]);
    int i = int(parts[3]);
    hand[h][i].x = lerp(hand[h][i].x, msg.get(0).floatValue(), 0.4);
    hand[h][i].y = lerp(hand[h][i].y, msg.get(1).floatValue(), 0.4);
    lastHandMs[h] = millis();
  }
  if (addr.startsWith("/fingers_proc")) {
     int val = msg.get(0).intValue();       // read the int
     if(val==1)
     {
       IndexFinger=true;}
      else if(val==2){
        MiddleFinger=true;
      }
       else if(val==3){
        RingFinger=true;
      }
       else if(val==4){
        PinkyFinger=true;
      }
      else if(val==5){
        IndexLeftFinger=true;
      }
       else if(val==6){
        MiddleLeftFinger=true;
      }
      else if(val==7)
      {
        IndexRightFinger=true;
      }
      else if(val==8)
      {
        MiddleRightFinger=true;
      }
     
      println("IndexFinger is now " + IndexFinger);
      println("MIddleFinger is now " + MiddleFinger);
      println("RingFinger is now " + RingFinger);
      println("PinkyFInger is now " + PinkyFinger);
      return;   
  }
  
   if (addr.equals("/activePage")) {
    currentPage = msg.get(0).stringValue();
    return;
  }
  if (msg.addrPattern().equals("/clearSynth")) {
    IndexFinger = MiddleFinger = RingFinger = PinkyFinger = false;
    return;
  } 
  if (msg.addrPattern().equals("/clearDrum")) {
    IndexLeftFinger = MiddleLeftFinger = IndexRightFinger = MiddleRightFinger = false;
    return;
  }
  
}
