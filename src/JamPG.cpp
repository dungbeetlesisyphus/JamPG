// JamPG — Image-to-Rhythm Sequencer
// CORPUS Module for VCV Rack Pro 2
//
// Your image is a drum machine — JamPG reads pixels as rhythm.
// Each clock pulse advances through the image pixel-by-pixel.
// R/G/B channels become three independent gate + CV voices.
//
// Architecture: pure CPU, no external deps beyond stb_image.h
// Image loading happens on the UI thread via osdialog.
// Pixel reading is O(1) per clock — no DSP to optimize.

#include "plugin.hpp"
#include "CorpusWidgets.hpp"
#include <osdialog.h>
#include <cmath>
#include <mutex>

// stb_image — single-header JPEG/PNG/BMP decoder
// Implementation lives in a separate compilation unit to avoid
// multiple-definition errors if any other module also uses stb.
// We guard it here since CORPUS currently has no other stb user.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_THREAD_LOCAL  // avoid TLS issues on some platforms
#include "dsp/stb_image.h"

// ═══════════════════════════════════════════════════════════════════
// Accent color — JamPG uses a warm amber/gold to stand out from
// the body-tracking modules (blue) and CORE mesh modules (teal).
// Amber evokes CRT phosphor glow.
// ═══════════════════════════════════════════════════════════════════

namespace JamPGColors {
    // ── Panel accent ──
    static const NVGcolor AMBER      = nvgRGB(0xC4, 0x8B, 0x2F);
    static const NVGcolor AMBER_DIM  = nvgRGBA(0xC4, 0x8B, 0x2F, 0x60);

    // ── RGB voice channels ──
    static const NVGcolor RED_VOICE  = nvgRGB(0xC4, 0x4B, 0x4F);
    static const NVGcolor GRN_VOICE  = nvgRGB(0x4B, 0x9E, 0x5B);
    static const NVGcolor BLU_VOICE  = nvgRGB(0x4B, 0x6E, 0xC4);

    // ── CRT display colors ──
    static const NVGcolor DISPLAY_BG    = nvgRGB(0x06, 0x08, 0x0C);   // deep CRT black
    static const NVGcolor GLASS_EDGE    = nvgRGBA(0x18, 0x1C, 0x28, 0xC0); // inner bezel reflection
    static const NVGcolor DIM_TEXT      = nvgRGBA(0x8A, 0x6E, 0x3A, 0x90); // phosphor amber, dim
    static const NVGcolor PHOSPHOR_TEXT = nvgRGBA(0xC4, 0x8B, 0x2F, 0xB0); // phosphor amber, bright
    static const NVGcolor CROSSHAIR    = nvgRGB(0x00, 0xFF, 0xB3);
    static const NVGcolor SCANLINE     = nvgRGBA(0xC4, 0x8B, 0x2F, 0x08); // faint amber scan lines
    static const NVGcolor VIGNETTE     = nvgRGBA(0x00, 0x00, 0x00, 0x60); // CRT edge darkening
}

// ═══════════════════════════════════════════════════════════════════
// Module
// ═══════════════════════════════════════════════════════════════════

struct JamPG : Module {
    enum ParamId {
        RED_LEVEL_PARAM,
        GREEN_LEVEL_PARAM,
        BLUE_LEVEL_PARAM,
        THRESHOLD_PARAM,
        SCAN_WIDTH_PARAM,
        LOAD_IMAGE_PARAM,
        RESET_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RESET_TRIG_INPUT,
        RED_CV_INPUT,
        GREEN_CV_INPUT,
        BLUE_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        RED_GATE_OUTPUT,
        GREEN_GATE_OUTPUT,
        BLUE_GATE_OUTPUT,
        RED_CV_OUTPUT,
        GREEN_CV_OUTPUT,
        BLUE_CV_OUTPUT,
        POSITION_X_OUTPUT,
        POSITION_Y_OUTPUT,
        EOL_TRIG_OUTPUT,
        EOF_TRIG_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        RED_ACTIVE_LIGHT,
        GREEN_ACTIVE_LIGHT,
        BLUE_ACTIVE_LIGHT,
        IMAGE_LOADED_LIGHT,
        LIGHTS_LEN
    };

    // ── Image data ──
    // Protected by imageMutex for load/free; audio thread only
    // reads imageData/Width/Height which are set atomically after load.
    unsigned char* imageData = nullptr;
    int imageWidth  = 0;
    int imageHeight = 0;
    std::string loadedImagePath;
    std::mutex imageMutex;

    // Dirty flag for display widget to know when to rebuild NVG texture
    bool imageDirty = true;

    // Flag: audio thread requests file dialog, UI thread fulfills it
    bool requestLoadDialog = false;

    // ── Playhead state ──
    int playheadX = 0;
    int playheadY = 0;

    // ── Trigger detection ──
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger resetButtonTrigger;
    dsp::SchmittTrigger loadButtonTrigger;

    // ── Pulse generators for EOL/EOF ──
    dsp::PulseGenerator eolPulse;
    dsp::PulseGenerator eofPulse;

    // ── Current pixel RGB (normalized 0–1) ──
    float currentRed   = 0.f;
    float currentGreen = 0.f;
    float currentBlue  = 0.f;

    // ── Smoothed params ──
    float redLevelSmooth   = 0.8f;
    float greenLevelSmooth = 0.8f;
    float blueLevelSmooth  = 0.8f;
    float thresholdSmooth  = 0.15f;
    float scanWidthSmooth  = 64.f;

    JamPG() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(RED_LEVEL_PARAM,   0.f, 1.f, 0.8f, "Red Level",   "%", 0.f, 100.f);
        configParam(GREEN_LEVEL_PARAM, 0.f, 1.f, 0.8f, "Green Level", "%", 0.f, 100.f);
        configParam(BLUE_LEVEL_PARAM,  0.f, 1.f, 0.8f, "Blue Level",  "%", 0.f, 100.f);
        configParam(THRESHOLD_PARAM,   0.f, 1.f, 0.15f, "Threshold",  "%", 0.f, 100.f);
        configParam(SCAN_WIDTH_PARAM,  1.f, 32.f, 1.f, "Pixel Step", "px", 0.f, 1.f);
        configParam(LOAD_IMAGE_PARAM,  0.f, 1.f, 0.f, "Load Image");
        configParam(RESET_PARAM,       0.f, 1.f, 0.f, "Reset Playhead");

        configInput(CLOCK_INPUT,      "Clock");
        configInput(RESET_TRIG_INPUT, "Reset Trigger");
        configInput(RED_CV_INPUT,     "Red Level CV");
        configInput(GREEN_CV_INPUT,   "Green Level CV");
        configInput(BLUE_CV_INPUT,    "Blue Level CV");

        configOutput(RED_GATE_OUTPUT,   "Red Gate");
        configOutput(GREEN_GATE_OUTPUT, "Green Gate");
        configOutput(BLUE_GATE_OUTPUT,  "Blue Gate");
        configOutput(RED_CV_OUTPUT,     "Red CV");
        configOutput(GREEN_CV_OUTPUT,   "Green CV");
        configOutput(BLUE_CV_OUTPUT,    "Blue CV");
        configOutput(POSITION_X_OUTPUT, "X Position CV");
        configOutput(POSITION_Y_OUTPUT, "Y Position CV");
        configOutput(EOL_TRIG_OUTPUT,   "End of Line Trigger");
        configOutput(EOF_TRIG_OUTPUT,   "End of File Trigger");
    }

    ~JamPG() {
        std::lock_guard<std::mutex> lock(imageMutex);
        if (imageData) {
            stbi_image_free(imageData);
            imageData = nullptr;
        }
    }

    // ── Image loading (called from UI thread via button or JSON restore) ──

    void loadImage(const std::string& path) {
        int w = 0, h = 0, ch = 0;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 3);

        std::lock_guard<std::mutex> lock(imageMutex);
        if (imageData) {
            stbi_image_free(imageData);
        }

        if (data) {
            imageData   = data;
            imageWidth  = w;
            imageHeight = h;
            loadedImagePath = path;
            playheadX = 0;
            playheadY = 0;
            imageDirty = true;
        } else {
            imageData   = nullptr;
            imageWidth  = 0;
            imageHeight = 0;
            loadedImagePath.clear();
            imageDirty = true;
        }
    }

    void loadImageDialog() {
        osdialog_filters* filters = osdialog_filters_parse("Image:jpg,jpeg,png,bmp");
        char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
        osdialog_filters_free(filters);
        if (path) {
            loadImage(path);
            std::free(path);
        }
    }

    // ── Playhead logic ──

    void resetPlayhead() {
        playheadX = 0;
        playheadY = 0;
    }

    void readPixelAtPlayhead() {
        if (!imageData || imageWidth == 0 || imageHeight == 0) {
            currentRed = currentGreen = currentBlue = 0.f;
            return;
        }

        // playheadX/Y are in actual image pixel coordinates
        int x = clamp(playheadX, 0, imageWidth - 1);
        int y = clamp(playheadY, 0, imageHeight - 1);

        // Row-major RGB, 3 bytes per pixel
        int idx = (y * imageWidth + x) * 3;
        currentRed   = (float)imageData[idx + 0] / 255.f;
        currentGreen = (float)imageData[idx + 1] / 255.f;
        currentBlue  = (float)imageData[idx + 2] / 255.f;
    }

    void advancePlayhead() {
        if (!imageData || imageWidth == 0 || imageHeight == 0) return;

        // SCAN WIDTH = pixel step size (1 = every pixel, 4 = every 4th pixel)
        int step = std::max(1, (int)scanWidthSmooth);

        playheadX += step;

        // End of row → next row, fire EOL trigger
        if (playheadX >= imageWidth) {
            playheadX = 0;
            playheadY += step;
            eolPulse.trigger(1e-3f);

            // End of image → back to top-left, fire EOF trigger
            if (playheadY >= imageHeight) {
                playheadY = 0;
                eofPulse.trigger(1e-3f);
            }
        }
    }

    // ── Audio processing ──

    void process(const ProcessArgs& args) override {
        // Parameter smoothing (one-pole, ~3ms at 44.1k)
        float a = 1.f - std::exp(-args.sampleTime / 0.003f);
        redLevelSmooth   += (params[RED_LEVEL_PARAM].getValue()   - redLevelSmooth)   * a;
        greenLevelSmooth += (params[GREEN_LEVEL_PARAM].getValue() - greenLevelSmooth) * a;
        blueLevelSmooth  += (params[BLUE_LEVEL_PARAM].getValue()  - blueLevelSmooth)  * a;
        thresholdSmooth  += (params[THRESHOLD_PARAM].getValue()   - thresholdSmooth)  * a;
        scanWidthSmooth  += (params[SCAN_WIDTH_PARAM].getValue()  - scanWidthSmooth)  * a;

        // Load button (momentary) — set flag for UI thread to open dialog
        if (loadButtonTrigger.process(params[LOAD_IMAGE_PARAM].getValue() > 0.f)) {
            requestLoadDialog = true;
        }

        // Reset (button + trigger input)
        bool rst = resetButtonTrigger.process(params[RESET_PARAM].getValue() > 0.f);
        if (inputs[RESET_TRIG_INPUT].isConnected())
            rst |= resetTrigger.process(inputs[RESET_TRIG_INPUT].getVoltage());
        if (rst) resetPlayhead();

        // Clock → advance + read pixel
        if (inputs[CLOCK_INPUT].isConnected()) {
            if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage())) {
                advancePlayhead();
                readPixelAtPlayhead();
            }
        }

        // CV modulation on levels (additive, ±10V → ±1.0)
        float rLevel = clamp(redLevelSmooth   + (inputs[RED_CV_INPUT].isConnected()   ? inputs[RED_CV_INPUT].getVoltage()   / 10.f : 0.f), 0.f, 1.f);
        float gLevel = clamp(greenLevelSmooth + (inputs[GREEN_CV_INPUT].isConnected() ? inputs[GREEN_CV_INPUT].getVoltage() / 10.f : 0.f), 0.f, 1.f);
        float bLevel = clamp(blueLevelSmooth  + (inputs[BLUE_CV_INPUT].isConnected()  ? inputs[BLUE_CV_INPUT].getVoltage()  / 10.f : 0.f), 0.f, 1.f);

        // Gates (threshold comparison)
        bool rGate = currentRed   > thresholdSmooth;
        bool gGate = currentGreen > thresholdSmooth;
        bool bGate = currentBlue  > thresholdSmooth;

        outputs[RED_GATE_OUTPUT].setVoltage(rGate   ? 10.f : 0.f);
        outputs[GREEN_GATE_OUTPUT].setVoltage(gGate ? 10.f : 0.f);
        outputs[BLUE_GATE_OUTPUT].setVoltage(bGate  ? 10.f : 0.f);

        // Continuous CV (0–10V, scaled by level)
        outputs[RED_CV_OUTPUT].setVoltage(currentRed     * rLevel * 10.f);
        outputs[GREEN_CV_OUTPUT].setVoltage(currentGreen * gLevel * 10.f);
        outputs[BLUE_CV_OUTPUT].setVoltage(currentBlue   * bLevel * 10.f);

        // Position outputs (normalized 0–10V across actual image dimensions)
        if (imageData && imageWidth > 0 && imageHeight > 0) {
            outputs[POSITION_X_OUTPUT].setVoltage((float)playheadX / (float)imageWidth * 10.f);
            outputs[POSITION_Y_OUTPUT].setVoltage((float)playheadY / (float)imageHeight * 10.f);
        } else {
            outputs[POSITION_X_OUTPUT].setVoltage(0.f);
            outputs[POSITION_Y_OUTPUT].setVoltage(0.f);
        }

        // EOL/EOF pulse outputs
        outputs[EOL_TRIG_OUTPUT].setVoltage(eolPulse.process(args.sampleTime) ? 10.f : 0.f);
        outputs[EOF_TRIG_OUTPUT].setVoltage(eofPulse.process(args.sampleTime) ? 10.f : 0.f);

        // Lights
        lights[RED_ACTIVE_LIGHT].setBrightness(rGate ? 1.f : 0.f);
        lights[GREEN_ACTIVE_LIGHT].setBrightness(gGate ? 1.f : 0.f);
        lights[BLUE_ACTIVE_LIGHT].setBrightness(bGate ? 1.f : 0.f);
        lights[IMAGE_LOADED_LIGHT].setBrightness(imageData ? 1.f : 0.f);
    }

    // ── JSON serialization ──

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "imagePath", json_string(loadedImagePath.c_str()));
        json_object_set_new(root, "playheadX", json_integer(playheadX));
        json_object_set_new(root, "playheadY", json_integer(playheadY));
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* pathJ = json_object_get(root, "imagePath");
        if (pathJ) {
            std::string path = json_string_value(pathJ);
            if (!path.empty()) loadImage(path);
        }
        json_t* xJ = json_object_get(root, "playheadX");
        if (xJ) playheadX = json_integer_value(xJ);
        json_t* yJ = json_object_get(root, "playheadY");
        if (yJ) playheadY = json_integer_value(yJ);
    }

    void onReset() override {
        std::lock_guard<std::mutex> lock(imageMutex);
        if (imageData) {
            stbi_image_free(imageData);
            imageData = nullptr;
        }
        imageWidth = imageHeight = 0;
        loadedImagePath.clear();
        imageDirty = true;
        resetPlayhead();
        currentRed = currentGreen = currentBlue = 0.f;
    }
};


// ═══════════════════════════════════════════════════════════════════
// Image Preview Display
// Cached NanoVG texture — only recreated when image changes.
// ═══════════════════════════════════════════════════════════════════

struct JamPGDisplay : TransparentWidget {
    JamPG* module = nullptr;
    int cachedImage = -1;  // NVG image handle
    int cachedW = 0;
    int cachedH = 0;

    ~JamPGDisplay() {
        // NVG handles are owned by the context, cleaned up on context destroy.
        // We don't delete here because the vg context may already be gone.
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        float W = box.size.x;
        float H = box.size.y;

        // ── CRT glass background ──
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, W, H, 1.5f);
        nvgFillColor(args.vg, JamPGColors::DISPLAY_BG);
        nvgFill(args.vg);

        // Inner bezel edge glow (simulates CRT glass curvature catching light)
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, W - 1.f, H - 1.f, 1.5f);
        nvgStrokeColor(args.vg, JamPGColors::GLASS_EDGE);
        nvgStrokeWidth(args.vg, 0.75f);
        nvgStroke(args.vg);

        if (!module || !module->imageData) {
            // ── Placeholder: CRT "no signal" state ──
            // Faint scan lines across empty display
            for (float y = 0; y < H; y += 2.0f) {
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, 0, y);
                nvgLineTo(args.vg, W, y);
                nvgStrokeColor(args.vg, JamPGColors::SCANLINE);
                nvgStrokeWidth(args.vg, 0.5f);
                nvgStroke(args.vg);
            }

            // Phosphor amber text
            nvgFontSize(args.vg, 11.f);
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

            // Glow behind text
            nvgFillColor(args.vg, nvgRGBA(0xC4, 0x8B, 0x2F, 0x15));
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, W * 0.25f, H * 0.35f, W * 0.5f, H * 0.3f, 3.f);
            nvgFill(args.vg);

            nvgFillColor(args.vg, JamPGColors::PHOSPHOR_TEXT);
            nvgText(args.vg, W / 2.f, H / 2.f - 6.f, "NO SIGNAL", NULL);
            nvgFontSize(args.vg, 7.5f);
            nvgFillColor(args.vg, JamPGColors::DIM_TEXT);
            nvgText(args.vg, W / 2.f, H / 2.f + 7.f, "press LOAD", NULL);

            // Vignette overlay (CRT edge darkening)
            drawVignette(args.vg, W, H);
            return;
        }

        // ── Rebuild NVG texture if image changed ──
        if (module->imageDirty || cachedImage < 0 ||
            cachedW != module->imageWidth || cachedH != module->imageHeight) {
            if (cachedImage >= 0) {
                nvgDeleteImage(args.vg, cachedImage);
            }
            // stb_image loaded as RGB, but NanoVG expects RGBA
            int w = module->imageWidth;
            int h = module->imageHeight;
            std::vector<unsigned char> rgba(w * h * 4);
            for (int i = 0; i < w * h; i++) {
                rgba[i * 4 + 0] = module->imageData[i * 3 + 0];
                rgba[i * 4 + 1] = module->imageData[i * 3 + 1];
                rgba[i * 4 + 2] = module->imageData[i * 3 + 2];
                rgba[i * 4 + 3] = 255;
            }
            cachedImage = nvgCreateImageRGBA(args.vg, w, h, 0, rgba.data());
            cachedW = w;
            cachedH = h;
            module->imageDirty = false;
        }

        if (cachedImage < 0) return;

        // ── Fit image maintaining aspect ratio ──
        float imgAspect = (float)cachedW / (float)cachedH;
        float boxAspect = W / H;
        float drawW, drawH;

        if (imgAspect > boxAspect) {
            drawW = W;
            drawH = W / imgAspect;
        } else {
            drawH = H;
            drawW = H * imgAspect;
        }

        float offX = (W - drawW) / 2.f;
        float offY = (H - drawH) / 2.f;

        // ── Draw image (slightly dimmed — CRT phosphor never 100% bright) ──
        NVGpaint imgPaint = nvgImagePattern(args.vg, offX, offY, drawW, drawH, 0, cachedImage, 0.85f);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, offX, offY, drawW, drawH, 1.f);
        nvgFillPaint(args.vg, imgPaint);
        nvgFill(args.vg);

        // ── CRT scan lines over image ──
        for (float y = offY; y < offY + drawH; y += 2.0f) {
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, offX, y);
            nvgLineTo(args.vg, offX + drawW, y);
            nvgStrokeColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0x1A));
            nvgStrokeWidth(args.vg, 0.6f);
            nvgStroke(args.vg);
        }

        // ── Playhead crosshair with phosphor glow ──
        float crossX = offX + ((float)module->playheadX / (float)module->imageWidth) * drawW;
        float crossY = offY + ((float)module->playheadY / (float)module->imageHeight) * drawH;

        // Outer glow halo (phosphor bloom)
        NVGpaint glow = nvgRadialGradient(args.vg, crossX, crossY, 0.f, 8.f,
            nvgRGBA(0x00, 0xFF, 0xB3, 0x40), nvgRGBA(0x00, 0xFF, 0xB3, 0x00));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, crossX, crossY, 8.f);
        nvgFillPaint(args.vg, glow);
        nvgFill(args.vg);

        // Inner bright core
        NVGpaint core = nvgRadialGradient(args.vg, crossX, crossY, 0.f, 2.5f,
            nvgRGBA(0x00, 0xFF, 0xB3, 0x90), nvgRGBA(0x00, 0xFF, 0xB3, 0x00));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, crossX, crossY, 2.5f);
        nvgFillPaint(args.vg, core);
        nvgFill(args.vg);

        // Crosshair lines
        nvgStrokeWidth(args.vg, 0.8f);
        nvgStrokeColor(args.vg, nvgRGBA(0x00, 0xFF, 0xB3, 0xC0));
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, crossX - 5.f, crossY);
        nvgLineTo(args.vg, crossX + 5.f, crossY);
        nvgMoveTo(args.vg, crossX, crossY - 5.f);
        nvgLineTo(args.vg, crossX, crossY + 5.f);
        nvgStroke(args.vg);

        // Scan line indicator (horizontal, full width — CRT beam trace)
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, 0, crossY);
        nvgLineTo(args.vg, W, crossY);
        nvgStrokeColor(args.vg, nvgRGBA(0x00, 0xFF, 0xB3, 0x12));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        // ── Current pixel color swatch (bottom-right corner) ──
        float swatchSize = 7.f;
        float sx = W - swatchSize - 3.f;
        float sy = H - swatchSize - 3.f;

        // Swatch phosphor glow
        NVGpaint swGlow = nvgRadialGradient(args.vg,
            sx + swatchSize / 2.f, sy + swatchSize / 2.f, 0.f, swatchSize,
            nvgRGBAf(module->currentRed, module->currentGreen, module->currentBlue, 0.25f),
            nvgRGBAf(0, 0, 0, 0));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, sx + swatchSize / 2.f, sy + swatchSize / 2.f, swatchSize);
        nvgFillPaint(args.vg, swGlow);
        nvgFill(args.vg);

        // Swatch fill
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, sx, sy, swatchSize, swatchSize, 1.f);
        nvgFillColor(args.vg, nvgRGBf(module->currentRed, module->currentGreen, module->currentBlue));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(0x40, 0x40, 0x40, 0x60));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        // ── Row progress bar (amber phosphor sweep at bottom) ──
        float progress = (module->imageWidth > 0)
            ? (float)module->playheadX / (float)module->imageWidth
            : 0.f;
        float barH = 1.5f;
        // Progress fill
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, H - barH, W * progress, barH);
        nvgFillColor(args.vg, nvgRGBA(0xC4, 0x8B, 0x2F, 0x90));
        nvgFill(args.vg);
        // Bright leading edge (beam spot)
        if (progress > 0.01f) {
            float beamX = W * progress;
            NVGpaint beam = nvgRadialGradient(args.vg, beamX, H - barH / 2.f, 0.f, 3.f,
                nvgRGBA(0xD4, 0xA0, 0x30, 0xA0), nvgRGBA(0xD4, 0xA0, 0x30, 0x00));
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, beamX, H - barH / 2.f, 3.f);
            nvgFillPaint(args.vg, beam);
            nvgFill(args.vg);
        }

        // ── CRT vignette (edge darkening) ──
        drawVignette(args.vg, W, H);
    }

    // ── CRT vignette: darkens edges like a real cathode ray tube ──
    void drawVignette(NVGcontext* vg, float W, float H) {
        // Top edge
        NVGpaint vigTop = nvgLinearGradient(vg, 0, 0, 0, H * 0.15f,
            JamPGColors::VIGNETTE, nvgRGBA(0, 0, 0, 0));
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, W, H * 0.15f);
        nvgFillPaint(vg, vigTop);
        nvgFill(vg);

        // Bottom edge
        NVGpaint vigBot = nvgLinearGradient(vg, 0, H, 0, H * 0.85f,
            JamPGColors::VIGNETTE, nvgRGBA(0, 0, 0, 0));
        nvgBeginPath(vg);
        nvgRect(vg, 0, H * 0.85f, W, H * 0.15f);
        nvgFillPaint(vg, vigBot);
        nvgFill(vg);

        // Left edge
        NVGpaint vigL = nvgLinearGradient(vg, 0, 0, W * 0.1f, 0,
            JamPGColors::VIGNETTE, nvgRGBA(0, 0, 0, 0));
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, W * 0.1f, H);
        nvgFillPaint(vg, vigL);
        nvgFill(vg);

        // Right edge
        NVGpaint vigR = nvgLinearGradient(vg, W, 0, W * 0.9f, 0,
            JamPGColors::VIGNETTE, nvgRGBA(0, 0, 0, 0));
        nvgBeginPath(vg);
        nvgRect(vg, W * 0.9f, 0, W * 0.1f, H);
        nvgFillPaint(vg, vigR);
        nvgFill(vg);
    }
};


// ═══════════════════════════════════════════════════════════════════
// Widget
// ═══════════════════════════════════════════════════════════════════

struct JamPGWidget : ModuleWidget {
    JamPGWidget(JamPG* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/JamPG.svg")));

        // Screws
        addChild(createWidget<CorpusScrew>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<CorpusScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<CorpusScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<CorpusScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // 16HP = 81.28mm
        float cx   = 40.64f;  // center X
        float colL = 12.0f;   // left column
        float colR = 69.28f;  // right column

        // ── Image preview display (large, top area) ──
        {
            auto* disp = new JamPGDisplay;
            disp->box.pos  = mm2px(Vec(4.f, 14.f));
            disp->box.size = mm2px(Vec(73.28f, 40.f));
            disp->module = module;
            addChild(disp);
        }

        // ── RGB activity LEDs (below display) ──
        addChild(createLightCentered<MediumLight<RedLight>>(
            mm2px(Vec(cx - 10.f, 57.f)), module, JamPG::RED_ACTIVE_LIGHT));
        addChild(createLightCentered<MediumLight<GreenLight>>(
            mm2px(Vec(cx, 57.f)), module, JamPG::GREEN_ACTIVE_LIGHT));
        addChild(createLightCentered<MediumLight<BlueLight>>(
            mm2px(Vec(cx + 10.f, 57.f)), module, JamPG::BLUE_ACTIVE_LIGHT));

        // ── Left column: RGB level knobs ──
        {
            auto* k = createParamCentered<CorpusKnobSmall>(mm2px(Vec(colL, 66.f)), module, JamPG::RED_LEVEL_PARAM);
            k->arcColor = JamPGColors::RED_VOICE;
            addParam(k);
        }
        {
            auto* k = createParamCentered<CorpusKnobSmall>(mm2px(Vec(colL, 78.f)), module, JamPG::GREEN_LEVEL_PARAM);
            k->arcColor = JamPGColors::GRN_VOICE;
            addParam(k);
        }
        {
            auto* k = createParamCentered<CorpusKnobSmall>(mm2px(Vec(colL, 90.f)), module, JamPG::BLUE_LEVEL_PARAM);
            k->arcColor = JamPGColors::BLU_VOICE;
            addParam(k);
        }

        // ── Right column: Threshold + Scan Width ──
        {
            auto* k = createParamCentered<CorpusKnob>(mm2px(Vec(colR, 66.f)), module, JamPG::THRESHOLD_PARAM);
            k->arcColor = JamPGColors::AMBER;
            addParam(k);
        }
        {
            auto* k = createParamCentered<CorpusKnob>(mm2px(Vec(colR, 82.f)), module, JamPG::SCAN_WIDTH_PARAM);
            k->arcColor = JamPGColors::AMBER;
            addParam(k);
        }

        // ── Center: LOAD and RESET buttons ──
        addParam(createParamCentered<LEDButton>(mm2px(Vec(cx - 8.f, 66.f)), module, JamPG::LOAD_IMAGE_PARAM));
        addChild(createLightCentered<SmallLight<WhiteLight>>(mm2px(Vec(cx - 8.f, 60.f)), module, JamPG::IMAGE_LOADED_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(cx + 8.f, 66.f)), module, JamPG::RESET_PARAM));

        // ── RGB CV modulation inputs (center row, below level knobs) ──
        addInput(createInputCentered<CorpusPortIn>(mm2px(Vec(25.5f, 82.f)),  module, JamPG::RED_CV_INPUT));
        addInput(createInputCentered<CorpusPortIn>(mm2px(Vec(37.5f, 82.f)),  module, JamPG::GREEN_CV_INPUT));
        addInput(createInputCentered<CorpusPortIn>(mm2px(Vec(50.64f, 82.f)), module, JamPG::BLUE_CV_INPUT));

        // ── Clock + Reset inputs (bottom-left) ──
        addInput(createInputCentered<CorpusPortIn>(mm2px(Vec(colL, 115.5f)), module, JamPG::CLOCK_INPUT));
        addInput(createInputCentered<CorpusPortIn>(mm2px(Vec(24.f,  115.5f)), module, JamPG::RESET_TRIG_INPUT));

        // ── Gate outputs (right, stacked @ 10mm spacing) ──
        addOutput(createOutputCentered<CorpusPort>(mm2px(Vec(colR, 98.f)),  module, JamPG::RED_GATE_OUTPUT));
        addOutput(createOutputCentered<CorpusPort>(mm2px(Vec(colR, 108.f)), module, JamPG::GREEN_GATE_OUTPUT));
        addOutput(createOutputCentered<CorpusPort>(mm2px(Vec(colR, 118.f)), module, JamPG::BLUE_GATE_OUTPUT));

        // ── CV outputs (right-center @ 10mm spacing) ──
        addOutput(createOutputCentered<CorpusPort>(mm2px(Vec(57.f, 98.f)),  module, JamPG::RED_CV_OUTPUT));
        addOutput(createOutputCentered<CorpusPort>(mm2px(Vec(57.f, 108.f)), module, JamPG::GREEN_CV_OUTPUT));
        addOutput(createOutputCentered<CorpusPort>(mm2px(Vec(57.f, 118.f)), module, JamPG::BLUE_CV_OUTPUT));

        // ── Position + trigger outputs (center, 10mm spacing from CV inputs) ──
        addOutput(createOutputCentered<CorpusPort>(mm2px(Vec(36.f, 120.f)), module, JamPG::POSITION_X_OUTPUT));
        addOutput(createOutputCentered<CorpusPort>(mm2px(Vec(45.f, 120.f)), module, JamPG::POSITION_Y_OUTPUT));
        addOutput(createOutputCentered<CorpusPort>(mm2px(Vec(36.f, 110.f)), module, JamPG::EOL_TRIG_OUTPUT));
        addOutput(createOutputCentered<CorpusPort>(mm2px(Vec(45.f, 110.f)), module, JamPG::EOF_TRIG_OUTPUT));
    }

    // ── UI-thread step: service audio-thread requests ──
    void step() override {
        ModuleWidget::step();
        JamPG* m = dynamic_cast<JamPG*>(module);
        if (m && m->requestLoadDialog) {
            m->requestLoadDialog = false;
            m->loadImageDialog();
        }
    }

    void appendContextMenu(Menu* menu) override {
        ModuleWidget::appendContextMenu(menu);
        JamPG* m = dynamic_cast<JamPG*>(module);
        if (!m) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("─── JamPG ───"));

        // Load image menu item
        menu->addChild(createMenuItem("Load Image...", "", [=]() {
            m->loadImageDialog();
        }));

        // Show current image path
        if (!m->loadedImagePath.empty()) {
            // Extract just the filename
            std::string filename = m->loadedImagePath;
            size_t lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
                filename = filename.substr(lastSlash + 1);
            menu->addChild(createMenuLabel("Image: " + filename));

            std::string dims = std::to_string(m->imageWidth) + "×" + std::to_string(m->imageHeight);
            menu->addChild(createMenuLabel("Size: " + dims));
        }
    }
};

Model* modelJamPG = createModel<JamPG, JamPGWidget>("JamPG");
