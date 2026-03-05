#pragma once
#include <rack.hpp>

using namespace rack;

extern Plugin* pluginInstance;

// ─────────────────────────────────────────────────
// Corpus Color Palette
// ─────────────────────────────────────────────────

namespace CorpusColors {
    static const NVGcolor ESPRESSO      = nvgRGB(0x3A, 0x2E, 0x28);
    static const NVGcolor CREAM         = nvgRGB(0xE8, 0xDF, 0xD4);
    static const NVGcolor BONE          = nvgRGB(0xF5, 0xF0, 0xEB);
    static const NVGcolor BROWN_MED     = nvgRGB(0x6B, 0x5D, 0x50);
    static const NVGcolor BROWN_LIGHT   = nvgRGB(0xB8, 0xAA, 0x98);
    static const NVGcolor RED_ACCENT    = nvgRGB(0xC4, 0x4B, 0x4F);
    static const NVGcolor GREEN_ACCENT  = nvgRGB(0x2D, 0x5A, 0x27);
    static const NVGcolor BLUE_ACCENT   = nvgRGB(0x3B, 0x6E, 0x8C);
    static const NVGcolor AMBER_ACCENT  = nvgRGB(0xC4, 0x8B, 0x2F);
}

// ─────────────────────────────────────────────────
// Large Knob — SVG base + NanoVG value arc overlay
// ─────────────────────────────────────────────────

struct CorpusKnob : app::SvgKnob {
    NVGcolor arcColor = CorpusColors::BROWN_LIGHT;

    CorpusKnob() {
        minAngle = -0.83f * M_PI;
        maxAngle = 0.83f * M_PI;
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/CorpusKnob.svg")));
        shadow->opacity = 0.0f; // SVG has its own shadow
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            auto pq = getParamQuantity();
            if (!pq) { SvgKnob::drawLayer(args, layer); return; }

            float angle = math::rescale(
                pq->getValue(), pq->getMinValue(), pq->getMaxValue(),
                minAngle, maxAngle
            );

            float cx = box.size.x * 0.5f;
            float cy = box.size.y * 0.5f;
            float r = cx - 3.f;

            // Value arc — glows on Layer 1 (emissive)
            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, r, minAngle - M_PI_2, angle - M_PI_2, NVG_CW);
            nvgStrokeColor(args.vg, nvgTransRGBA(arcColor, 0x90));
            nvgStrokeWidth(args.vg, 1.8f);
            nvgLineCap(args.vg, NVG_ROUND);
            nvgStroke(args.vg);

            // Bright dot at current position
            float dotX = cx + r * std::cos(angle - M_PI_2);
            float dotY = cy + r * std::sin(angle - M_PI_2);
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, dotX, dotY, 2.f);
            nvgFillColor(args.vg, nvgTransRGBA(arcColor, 0xCC));
            nvgFill(args.vg);
        }
        SvgKnob::drawLayer(args, layer);
    }
};

// ─────────────────────────────────────────────────
// Small Knob — same concept, tighter arc
// ─────────────────────────────────────────────────

struct CorpusKnobSmall : app::SvgKnob {
    NVGcolor arcColor = CorpusColors::BROWN_LIGHT;

    CorpusKnobSmall() {
        minAngle = -0.83f * M_PI;
        maxAngle = 0.83f * M_PI;
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/CorpusKnobSmall.svg")));
        shadow->opacity = 0.0f;
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            auto pq = getParamQuantity();
            if (!pq) { SvgKnob::drawLayer(args, layer); return; }

            float angle = math::rescale(
                pq->getValue(), pq->getMinValue(), pq->getMaxValue(),
                minAngle, maxAngle
            );

            float cx = box.size.x * 0.5f;
            float cy = box.size.y * 0.5f;
            float r = cx - 2.f;

            nvgBeginPath(args.vg);
            nvgArc(args.vg, cx, cy, r, minAngle - M_PI_2, angle - M_PI_2, NVG_CW);
            nvgStrokeColor(args.vg, nvgTransRGBA(arcColor, 0x80));
            nvgStrokeWidth(args.vg, 1.3f);
            nvgLineCap(args.vg, NVG_ROUND);
            nvgStroke(args.vg);
        }
        SvgKnob::drawLayer(args, layer);
    }
};

// ─────────────────────────────────────────────────
// Output Port — dark body, subtle accent ring
// ─────────────────────────────────────────────────

struct CorpusPort : app::SvgPort {
    CorpusPort() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/CorpusPort.svg")));
        shadow->opacity = 0.0f;
    }
};

// ─────────────────────────────────────────────────
// Input Port — lighter bezel, cream inner ring
// ─────────────────────────────────────────────────

struct CorpusPortIn : app::SvgPort {
    CorpusPortIn() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/CorpusPortIn.svg")));
        shadow->opacity = 0.0f;
    }
};

// ─────────────────────────────────────────────────
// Custom Screw — warm dark metal
// ─────────────────────────────────────────────────

struct CorpusScrew : app::SvgScrew {
    CorpusScrew() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/CorpusScrew.svg")));
    }
};

// ─────────────────────────────────────────────────
// Lights — using stock VCV light widgets
// (Custom lights deferred to a future iteration)
// ─────────────────────────────────────────────────
// All modules use stock VCV lights:
//   Camera indicator:  SmallLight<GreenLight>
//   Activity lights:   MediumLight<RedLight>, MediumLight<BlueLight>,
//                      MediumLight<YellowLight>, SmallLight<GreenLight>,
//                      SmallLight<RedLight>
