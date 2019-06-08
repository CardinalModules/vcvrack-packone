#include "plugin.hpp"
#include "widgets.hpp"

struct RotorA : Module {
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

    simd::float_4 mask[4];
    float split;
    int channels;

    RotorA() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(CHANNELS_PARAM, 2, 16, 16, "Number of output channels");
      
        onReset();
        lightDivider.setDivision(2048);
        channelsDivider.setDivision(256);
        channels = ceil(params[CHANNELS_PARAM].getValue());
        split = 10.f / (float)(channels - 1); 
	}

    void process(const ProcessArgs &args) override {      
        if (channelsDivider.process()) {
            channels = ceil(params[CHANNELS_PARAM].getValue());
            for (int c = 0; c < 4; c++) {
                mask[c] = simd::float_4::mask();
            }
            for (int c = inputs[BASE_INPUT].getChannels(); c < 16; c++) {
                mask[c / 4].s[c % 4] = 0.f;
            }
            split = 10.f / (float)(channels - 1);      
        }

        float car = inputs[CAR_INPUT].isConnected() ? clamp(inputs[CAR_INPUT].getVoltage(), 0.f, 10.f) : 10.f;

        simd::float_4 v[4];
        for (int c = 0; c < 16; c += 4) {
            v[c / 4] = 0.f;           
        }
    
        float mod = clamp(inputs[MOD_INPUT].getVoltage(), 0.f, 10.f);
        float mod_p = mod / split;
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
                v1 = ifelse(mask[c / 4], v1, 1.f);
                v1 = v1 * v[c / 4];
                //float v = (c < inputs[BASE_INPUT].getChannels()) ? rescale(inputs[BASE_INPUT].getVoltage(c), 0.f, 10.f, 0.f, 1.f) : 1.f; 
                //.setVoltage(v * temp[c], c);
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

    /*/
    Menu* createContextMenu() override {
        Menu* theMenu = ModuleWidget::createContextMenu();
        ManualMenuItem* manual = new ManualMenuItem("https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/RotorA.md");
        return theMenu;
        theMenu->addChild(manual);
    }
    */
};


struct RotorAWidget : ModuleWidget {
	RotorAWidget(RotorA *module) {	
		setModule(module);
    	setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RotorA.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParamCentered<RoundBlackSnapKnob>(Vec(37.6f, 239.3f), module, RotorA::CHANNELS_PARAM));

        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 62.4f), module, RotorA::MOD_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 106.7f), module, RotorA::CAR_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(37.6f, 154.0f), module, RotorA::BASE_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(37.6f, 291.9f), module, RotorA::POLY_OUTPUT));

        PolyLedWidget *w0 = createWidgetCentered<PolyLedWidget>(Vec(37.6, 185.0f));
        w0->setModule(module, RotorA::INPUT_LIGHTS);
        addChild(w0);

        PolyLedWidget *w1 = createWidgetCentered<PolyLedWidget>(Vec(37.6, 322.1f));
        w1->setModule(module, RotorA::OUTPUT_LIGHTS);
        addChild(w1);
    }
};

Model *modelRotorA = createModel<RotorA, RotorAWidget>("RotorA");