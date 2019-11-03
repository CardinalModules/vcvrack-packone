#include "plugin.hpp"
#include <thread>


namespace RotorA {

struct RotorAModule : Module {
	enum ParamIds {
		CHANNELS_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		MOD_INPUT,
		CAR_INPUT,
		BASE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(INPUT_LIGHTS, 16),
		ENUMS(OUTPUT_LIGHTS, 16),
		NUM_LIGHTS
	};

	dsp::ClockDivider lightDivider;
	dsp::ClockDivider channelsDivider;

	int channels;
	simd::float_4 channelsMask[4];
	float channelsSplit;

	RotorAModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CHANNELS_PARAM, 2, 16, 16, "Number of output channels");

		onReset();
		lightDivider.setDivision(2048);
		channelsDivider.setDivision(512);
		channels = ceil(params[CHANNELS_PARAM].getValue());
		channelsSplit = 10.f / (float)(channels - 1);
	}

	void process(const ProcessArgs &args) override {
		// Update mask for input channels infrequently
		if (channelsDivider.process()) {
			channels = ceil(params[CHANNELS_PARAM].getValue());
			for (int c = 0; c < 4; c++) {
				channelsMask[c] = simd::float_4::mask();
			}
			for (int c = inputs[BASE_INPUT].getChannels(); c < 16; c++) {
				channelsMask[c / 4].s[c % 4] = 0.f;
			}
			channelsSplit = 10.f / (float)(channels - 1);
		}

		float car = inputs[CAR_INPUT].isConnected() ? clamp(inputs[CAR_INPUT].getVoltage(), 0.f, 10.f) : 10.f;

		simd::float_4 v[4];
		for (int c = 0; c < 16; c += 4) {
			v[c / 4] = 0.f;
		}

		float mod = clamp(inputs[MOD_INPUT].getVoltage(), 0.f, 10.f);
		float mod_p = mod / channelsSplit;
		int mod_c = floor(mod_p);
		float mod_p2 = mod_p - (float)mod_c;
		float mod_p1 = 1.f - mod_p2;

		v[(mod_c + 0) / 4].s[(mod_c + 0) % 4] = mod_p1 * car;
		v[(mod_c + 1) / 4].s[(mod_c + 1) % 4] = mod_p2 * car;

		if (outputs[POLY_OUTPUT].isConnected()) {
			outputs[POLY_OUTPUT].setChannels(channels);
			for (int c = 0; c < channels; c += 4) {
				simd::float_4 v1 = simd::float_4::load(inputs[BASE_INPUT].getVoltages(c));
				v1 = rescale(v1, 0.f, 10.f, 0.f, 1.f);
				v1 = ifelse(channelsMask[c / 4], v1, 1.f);
				v1 = v1 * v[c / 4];
				v1.store(outputs[POLY_OUTPUT].getVoltages(c));
			}
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			for (int c = 0; c < 16; c++) {
				bool active = (c < inputs[BASE_INPUT].getChannels());
				lights[INPUT_LIGHTS + c].setBrightness(active);
			}
			for (int c = 0; c < 16; c++) {
				bool active = (c < channels);
				lights[OUTPUT_LIGHTS + c].setBrightness(active);
			}
		}
	}
};


struct RotorAWidget : ModuleWidget {
	RotorAWidget(RotorAModule* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RotorA.svg")));

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<StoermelderPort>(Vec(30.f, 74.6f), module, RotorAModule::MOD_INPUT));
		addInput(createInputCentered<StoermelderPort>(Vec(30.f, 122.3f), module, RotorAModule::CAR_INPUT));

		PolyLedWidget<>* w0 = createWidgetCentered<PolyLedWidget<>>(Vec(30.f, 168.5f));
		w0->setModule(module, RotorAModule::INPUT_LIGHTS);
		addChild(w0);
		addInput(createInputCentered<StoermelderPort>(Vec(30.f, 194.5f), module, RotorAModule::BASE_INPUT));

		addParam(createParamCentered<RoundBlackSnapKnob>(Vec(30.f, 239.6f), module, RotorAModule::CHANNELS_PARAM));

		PolyLedWidget<>* w1 = createWidgetCentered<PolyLedWidget<>>(Vec(30.f, 299.8f));
		w1->setModule(module, RotorAModule::OUTPUT_LIGHTS);
		addChild(w1);
		addOutput(createOutputCentered<StoermelderPort>(Vec(30.f, 327.9f), module, RotorAModule::POLY_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		struct ManualItem : MenuItem {
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/RotorA.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
	};
};

} // namespace RotorA

Model* modelRotorA = createModel<RotorA::RotorAModule, RotorA::RotorAWidget>("RotorA");