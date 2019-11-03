#pragma once
#include "plugin.hpp"


struct LongPressButton {
	enum Events {
		NO_PRESS,
		SHORT_PRESS,
		LONG_PRESS
	};

	float pressedTime = 0.f;
	dsp::BooleanTrigger trigger;

	Events step(Param &param) {
		Events result = NO_PRESS;
		bool pressed = param.value > 0.f;
		if (pressed && pressedTime >= 0.f) {
			pressedTime += APP->engine->getSampleTime();
			if (pressedTime >= 1.f) {
				pressedTime = -1.f;
				result = LONG_PRESS;
			}
		}

		// Check if released
		if (trigger.process(!pressed)) {
			if (pressedTime >= 0.f) {
				result = SHORT_PRESS;
			}
			pressedTime = 0.f;
		}

		return result;
	}
};


template <typename TBase>
struct TriangleLeftLight : TBase {
	void drawLight(const widget::Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, this->box.size.x, 0);
		nvgLineTo(args.vg, this->box.size.x, this->box.size.y);
		nvgLineTo(args.vg, 0, this->box.size.y / 2.f);
		nvgClosePath(args.vg);

		// Background
		if (this->bgColor.a > 0.0) {
			nvgFillColor(args.vg, this->bgColor);
			nvgFill(args.vg);
		}

		// Foreground
		if (this->color.a > 0.0) {
			nvgFillColor(args.vg, this->color);
			nvgFill(args.vg);
		}

		// Border
		if (this->borderColor.a > 0.0) {
			nvgStrokeWidth(args.vg, 0.5);
			nvgStrokeColor(args.vg, this->borderColor);
			nvgStroke(args.vg);
		}
	}
};

template <typename TBase>
struct TriangleRightLight : TBase {
	void drawLight(const widget::Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0, 0);
		nvgLineTo(args.vg, 0, this->box.size.y);
		nvgLineTo(args.vg, this->box.size.x, this->box.size.y / 2.f);
		nvgClosePath(args.vg);

		// Background
		if (this->bgColor.a > 0.0) {
			nvgFillColor(args.vg, this->bgColor);
			nvgFill(args.vg);
		}

		// Foreground
		if (this->color.a > 0.0) {
			nvgFillColor(args.vg, this->color);
			nvgFill(args.vg);
		}

		// Border
		if (this->borderColor.a > 0.0) {
			nvgStrokeWidth(args.vg, 0.5);
			nvgStrokeColor(args.vg, this->borderColor);
			nvgStroke(args.vg);
		}
	}
};


struct StoermelderBlackScrew : app::SvgScrew {
	widget::TransformWidget* tw;

	StoermelderBlackScrew() {
		fb->removeChild(sw);

		tw = new TransformWidget();
		tw->addChild(sw);
		fb->addChild(tw);

		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/Screw.svg")));

		tw->box.size = sw->box.size;
		box.size = tw->box.size;

		float angle = random::uniform() * M_PI;
		tw->identity();
		// Rotate SVG
		math::Vec center = sw->box.getCenter();
		tw->translate(center);
		tw->rotate(angle);
		tw->translate(center.neg());
	}
};

struct StoermelderTrimpot : app::SvgKnob {
	StoermelderTrimpot() {
		minAngle = -0.75 * M_PI;
		maxAngle = 0.75 * M_PI;
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/Trimpot.svg")));
	}
};

struct StoermelderPort : app::SvgPort {
	StoermelderPort() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/components/Port.svg")));
		box.size = Vec(22.2f, 22.2f);
	}
};


template < typename LIGHT = BlueLight, int COLORS = 1>
struct PolyLedWidget : Widget {
	PolyLedWidget() {
		box.size = mm2px(Vec(6.f, 6.f));
	}

	void setModule(Module* module, int firstlightId) {
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 0)), module, firstlightId + COLORS * 0));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 0)), module, firstlightId + COLORS * 1));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 0)), module, firstlightId + COLORS * 2));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 0)), module, firstlightId + COLORS * 3));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 2)), module, firstlightId + COLORS * 4));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 2)), module, firstlightId + COLORS * 5));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 2)), module, firstlightId + COLORS * 6));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 2)), module, firstlightId + COLORS * 7));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 4)), module, firstlightId + COLORS * 8));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 4)), module, firstlightId + COLORS * 9));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 4)), module, firstlightId + COLORS * 10));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 4)), module, firstlightId + COLORS * 11));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(0, 6)), module, firstlightId + COLORS * 12));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(2, 6)), module, firstlightId + COLORS * 13));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(4, 6)), module, firstlightId + COLORS * 14));
		addChild(createLightCentered<TinyLight<LIGHT>>(mm2px(Vec(6, 6)), module, firstlightId + COLORS * 15));
	}
};