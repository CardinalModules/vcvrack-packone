#include "plugin.hpp"

namespace StoermelderPackOne {
namespace Spin {

enum class CLICK_MODE {
	TOGGLE = 0,
	TRIGGER = 1,
	GATE = 2
};

struct SpinModule : Module {
	enum ParamIds {
		PARAM_ONLY,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_DEC,
		OUTPUT_INC,
		OUTPUT_CLICK,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/** [Stored to JSON] */
	int panelTheme = 0;
	/** [Stored to JSON] */
	CLICK_MODE clickMode;
	/** [Stored to JSON] */
	bool clickHigh;

	float delta = 0.f;
	dsp::PulseGenerator decPulse;
	dsp::PulseGenerator incPulse;

	dsp::PulseGenerator clickPulse;

	SpinModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<TriggerParamQuantity>(PARAM_ONLY, 0.f, 1.f, 1.f, "Only active while parameter-hovering");
		onReset();
	}

	void onReset() override {
		Module::onReset();
		clickMode = CLICK_MODE::TOGGLE;
		clickHigh = false;
	}

	void process(const ProcessArgs& args) override {
		if (delta < 0.f) {
			incPulse.trigger();
			delta = 0.f;
		}
		if (delta > 0.f) {
			decPulse.trigger();
			delta = 0.f;
		}

		outputs[OUTPUT_INC].setVoltage(decPulse.process(args.sampleTime) * 10.f);
		outputs[OUTPUT_DEC].setVoltage(incPulse.process(args.sampleTime) * 10.f);

		switch (clickMode) {
			case CLICK_MODE::TRIGGER:
				outputs[OUTPUT_CLICK].setVoltage(clickPulse.process(args.sampleTime) * 10.f);
				break;
			case CLICK_MODE::GATE:
			case CLICK_MODE::TOGGLE:
				outputs[OUTPUT_CLICK].setVoltage(clickHigh * 10.f);
				break;
		}	
	}

	void clickEnable() {
		switch (clickMode) {
			case CLICK_MODE::TRIGGER:
				clickPulse.trigger(); break;
			case CLICK_MODE::GATE:
				clickHigh = true; break;
			case CLICK_MODE::TOGGLE:
				clickHigh ^= true; break;
		}
	}

	void clickDisable() {
		switch (clickMode) {
			case CLICK_MODE::GATE:
				clickHigh = false; break;
			default:
				break;
		}
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "clickMode", json_integer((int)clickMode));
		json_object_set_new(rootJ, "clickHigh", json_boolean(clickHigh));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
		clickMode = (CLICK_MODE)json_integer_value(json_object_get(rootJ, "clickMode"));
		clickHigh = json_boolean_value(json_object_get(rootJ, "clickHigh"));
	}
};


struct SpinContainer : widget::Widget {
	SpinModule* module;

	void onHoverScroll(const event::HoverScroll& e) override {
		if (module->params[SpinModule::PARAM_ONLY].getValue() == 1.f) {
			Widget* w = APP->event->getHoveredWidget();
			if (!w) return;
			ParamWidget* p = dynamic_cast<ParamWidget*>(w);
			if (!p) return;
			ParamQuantity* q = p->paramQuantity;
			if (!q) return;
		}

		module->delta = e.scrollDelta.y;
		e.consume(this);
	}

	void onButton(const event::Button& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_MIDDLE) {
			if (e.action == GLFW_PRESS && e.mods == 0) {
				module->clickEnable();
				e.consume(this);
			}
			if (e.action == RACK_HELD && e.mods == 0) {
				e.consume(this);
			}
			if (e.action == GLFW_RELEASE) {
				module->clickDisable();
				e.consume(this);
			}
		}
		Widget::onButton(e);
	}
};

struct SpinWidget : ThemedModuleWidget<SpinModule> {
	SpinContainer* mwContainer;

	SpinWidget(SpinModule* module) 
		: ThemedModuleWidget<SpinModule>(module, "Spin") {
		setModule(module);

		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 218.4f), module, SpinModule::OUTPUT_CLICK));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 254.8f), module, SpinModule::OUTPUT_INC));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 290.5f), module, SpinModule::OUTPUT_DEC));
		addParam(createParamCentered<CKSS>(Vec(22.5f, 332.9f), module, SpinModule::PARAM_ONLY));

		if (module) {
			mwContainer = new SpinContainer;
			mwContainer->module = module;
			// This is where the magic happens: add a new widget on top-level to Rack
			APP->scene->rack->addChild(mwContainer);
		}
	}

	~SpinWidget() {
		if (module) {
			APP->scene->rack->removeChild(mwContainer);
			delete mwContainer;
		}
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<SpinModule>::appendContextMenu(menu);
		SpinModule* module = dynamic_cast<SpinModule*>(this->module);

		struct ClickMenuItem : MenuItem {
			SpinModule* module;
			CLICK_MODE mode;
			void step() override {
				rightText = CHECKMARK(module->clickMode == mode);
				MenuItem::step();
			}
			void onAction(const event::Action& e) override {
				module->clickMode = mode;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Middle click mode"));
		menu->addChild(construct<ClickMenuItem>(&MenuItem::text, "Toggle", &ClickMenuItem::module, module, &ClickMenuItem::mode, CLICK_MODE::TOGGLE));
		menu->addChild(construct<ClickMenuItem>(&MenuItem::text, "Trigger", &ClickMenuItem::module, module, &ClickMenuItem::mode, CLICK_MODE::TRIGGER));
		menu->addChild(construct<ClickMenuItem>(&MenuItem::text, "Gate", &ClickMenuItem::module, module, &ClickMenuItem::mode, CLICK_MODE::GATE));
	}
};

} // namespace Spin
} // namespace StoermelderPackOne

Model* modelSpin = createModel<StoermelderPackOne::Spin::SpinModule, StoermelderPackOne::Spin::SpinWidget>("Spin");