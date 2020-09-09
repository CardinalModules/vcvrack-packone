#include "plugin.hpp"

/**

	https://dafx2020.mdw.ac.at/proceedings/papers/DAFx2020_paper_6.pdf

	Authors (students):		Alexander Ramirez (https://www.linkedin.com/in/ajramirezm/)
							Vikas Tokala (https://www.linkedin.com/in/vikastokala/)

	Supervisors: Antonin Novak, Frederic Ablitzer, Manuel Melon

	Online demo: https://ant-novak.com/posts/projects/2020-02-26_Bistable_Effect/


	International Master's Degree in ElectroAcoustics
	Le Mans University, 72085 Le Mans, France.
	contact address: antonin.novak@univ-lemans.fr
	June 2019; Last revision: 27-Feb-2020

**/

namespace StoermelderPackOne {
namespace Raw {

struct RawModule : Module {
	enum ParamIds {
		PARAM_GAIN_IN,
		PARAM_FN,
		PARAM_C,
		PARAM_K,
		PARAM_KMULT,
		PARAM_GAIN_OUT,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	simd::float_4 y[4][2];
	simd::float_4 x[4][3];
	float Ts, Ts0001;
	float A1, A2, A3;
	float m, c, k, k3, Fn, Wn, in_gain, out_gain;

	dsp::ClockDivider paramDivider;

	/** [Stored to JSON] */
	int panelTheme = 0;

	RawModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_GAIN_IN, -20.f, 20.f, 15.f, "Input gain", "dB");
		configParam(PARAM_FN, 20.f, 2000.f, 1000.f, "Resonance frequency", "Hz");
		configParam(PARAM_C, -6.f, -3.f, -4.f, "Damping coefficient");
		configParam(PARAM_K, 0.01f, 1.f, 0.5f, "Nonlinearity parameter");
		configParam(PARAM_KMULT, -1.f, 1.f, 0.f, "Nonlinearity asymmetry", "", 10.f);
		configParam(PARAM_GAIN_OUT, -20.f, 20.f, -10.f, "Output gain", "dB");
		onReset();
		paramDivider.setDivision(64);
	}

	void onReset() override {
		Module::onReset();
		for (int c = 0; c < 16; c += 4) {
			y[c / 4][0] = y[c / 4][1] = 0.f;
			x[c / 4][0] = x[c / 4][1] = x[c / 4][2] = 0.f;
		}
		paramDivider.reset();
	}

	void prepareParameters() {
		in_gain = pow(10.f, params[PARAM_GAIN_IN].getValue() / 20.f);
		// for normalization of input voltage [-5V,5V] to [-1,1]
		in_gain /= 5.0f;
		Fn = params[PARAM_FN].getValue();
		c = pow(10.f, params[PARAM_C].getValue());
		k = params[PARAM_K].getValue();
		k3 = k * pow(10.f, params[PARAM_KMULT].getValue());
		out_gain = pow(10.f, params[PARAM_GAIN_OUT].getValue() / 20.f);
		// for normalization of [-1,1] to output voltage [-5V,5V]
		out_gain *= 5.0f; 

		Ts = APP->engine->getSampleTime();
		Ts0001 = Ts / 0.0001f;

		// angular frequency
		Wn = 2.0f * M_PI * Fn;

		// mass
		m = k / pow(Wn, 2);

		// parameters for displacement equation
		A1 = m / pow(Ts, 2) + c / Ts;
		A2 = (-2 * m) / pow(Ts, 2) - c / Ts - k;
		A3 = m / pow(Ts, 2);
	}

	void process(const ProcessArgs& args) override {
		int channels = inputs[INPUT].getChannels();

		if (paramDivider.process()) {
			prepareParameters();
		}

		for (int c = 0; c < channels; c += 4) {
			y[c / 4][0] = inputs[INPUT].getPolyVoltageSimd<simd::float_4>(c) * in_gain;

			// displacement equation
			x[c / 4][0] = (y[c / 4][1] - A2 * x[c / 4][1] - A3 * x[c / 4][2] - k3 * pow(x[c / 4][1], 3.f)) / A1;

			// velocity (normalized by 10000)
			simd::float_4 v = (x[c / 4][0] - x[c / 4][1]) / Ts0001;

			// this implementation behaves unstable to do some stupid "limiting"
			// could possibly fixed with some oversampling, tbd
			simd::float_4 b = simd::abs(v) > 100.f;
			x[c / 4][0] = simd::ifelse(b, 0.f, x[c / 4][0]);
			x[c / 4][1] = simd::ifelse(b, 0.f, x[c / 4][1]);

			// shift buffers
			y[c / 4][1] = y[c / 4][0];
			x[c / 4][2] = x[c / 4][1];
			x[c / 4][1] = x[c / 4][0];

			outputs[OUTPUT].setVoltageSimd(v * out_gain, c);
		}

		outputs[OUTPUT].setChannels(channels);
	}

	json_t* dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));
	}
};


struct RawWidget : ThemedModuleWidget<RawModule> {
	RawWidget(RawModule* module)
 		: ThemedModuleWidget<RawModule>(module, "Raw") {
		setModule(module);

		addParam(createParamCentered<StoermelderSmallKnob>(Vec(22.5f, 61.1f), module, RawModule::PARAM_GAIN_IN));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(22.5f, 106.6f), module, RawModule::PARAM_FN));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(22.5f, 144.1f), module, RawModule::PARAM_C));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(22.5f, 181.6f), module, RawModule::PARAM_K));
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(22.5f, 201.6f), module, RawModule::PARAM_KMULT)); // temporary knob for testing new parameter
		addParam(createParamCentered<StoermelderSmallKnob>(Vec(22.5f, 228.1f), module, RawModule::PARAM_GAIN_OUT));

		addInput(createInputCentered<StoermelderPort>(Vec(22.5f, 283.5f), module, RawModule::INPUT));
		addOutput(createOutputCentered<StoermelderPort>(Vec(22.5f, 327.7f), module, RawModule::OUTPUT));
	}

	void appendContextMenu(Menu *menu) override {
		struct PublicationItem : MenuItem {
			void onAction(const event::Action& e) override {
				std::thread t(system::openBrowser, "https://dafx2020.mdw.ac.at/proceedings/papers/DAFx2020_paper_6.pdf");
				t.detach();
			}
		};

		menu->addChild(construct<PublicationItem>(&MenuItem::text, "Publication"));

		ThemedModuleWidget<RawModule>::appendContextMenu(menu);
		RawModule* module = dynamic_cast<RawModule*>(this->module);
		assert(module);

		struct PresetItem : MenuItem {
			RawModule* module;
			float in_gain, f, c, k, out_gain;
			void onAction(const event::Action& e) override {
				module->params[RawModule::PARAM_GAIN_IN].setValue(in_gain);
				module->params[RawModule::PARAM_FN].setValue(f);
				module->params[RawModule::PARAM_C].setValue(c);
				module->params[RawModule::PARAM_K].setValue(k);
				module->params[RawModule::PARAM_GAIN_OUT].setValue(out_gain);
				module->onReset();
			} 
		};

		PresetItem* p1 = construct<PresetItem>(&MenuItem::text, "Preset 1", &PresetItem::module, module);
		p1->in_gain = 0.f;
		p1->f = 300.f;
		p1->c = -4.f;
		p1->k = 1.f;
		p1->out_gain = 15.f;

		PresetItem* p2 = construct<PresetItem>(&MenuItem::text, "Preset 2", &PresetItem::module, module);
		p2->in_gain = 15.f;
		p2->f = 150.f;
		p2->c = -4.f;
		p2->k = 0.1f;
		p2->out_gain = -5.f;

		PresetItem* p3 = construct<PresetItem>(&MenuItem::text, "Preset 3", &PresetItem::module, module);
		p3->in_gain = 15.f;
		p3->f = 1000.f;
		p3->c = -4.f;
		p3->k = 0.5f;
		p3->out_gain = -10.f;

		PresetItem* p4 = construct<PresetItem>(&MenuItem::text, "Preset 4", &PresetItem::module, module);
		p4->in_gain = 0.f;
		p4->f = 200.f;
		p4->c = -5.f;
		p4->k = 0.2f;
		p4->out_gain = 0.f;

		menu->addChild(new MenuSeparator);
		menu->addChild(p1);
		menu->addChild(p2);
		menu->addChild(p3);
		menu->addChild(p4);
	}
};

} // namespace Raw
} // namespace StoermelderPackOne

Model* modelRaw = createModel<StoermelderPackOne::Raw::RawModule, StoermelderPackOne::Raw::RawWidget>("Raw");