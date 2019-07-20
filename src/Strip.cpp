#include "plugin.hpp"
#include "components.hpp"
#include <osdialog.h>
#include <plugin.hpp>
#include <thread>

static const char PRESET_FILTERS[] = "stoermelder STRIP group preset (.vcvss):vcvss";

const int STRIP_ONMODE_DEFAULT = 0;
const int STRIP_ONMODE_TOGGLE = 1;
const int STRIP_ONMODE_HIGHLOW = 2;

const int STRIP_MODE_LEFTRIGHT = 0;
const int STRIP_MODE_RIGHT = 1;
const int STRIP_MODE_LEFT = 2;


struct Strip : Module {
	enum ParamIds {
		MODE_PARAM,
		ON_PARAM,
		OFF_PARAM,
		RAND_PARAM,
		EXCLUDE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ON_INPUT,
		OFF_INPUT,
		RAND_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LEFT_LIGHT,
		RIGHT_LIGHT,
		ENUMS(EXCLUDE_LIGHT, 2),
		NUM_LIGHTS
	};

	/** [Stored to JSON] left? right? both? */
	int mode = STRIP_MODE_LEFTRIGHT;
	/** [Stored to JSON] usage of switch+port in "ON"-section */
	int onMode = STRIP_ONMODE_DEFAULT;

	bool lastState = false;

	dsp::SchmittTrigger modeTrigger;
	dsp::SchmittTrigger onTrigger;
	dsp::SchmittTrigger offPTrigger;
	dsp::SchmittTrigger randTrigger;

	LongPressButton excludeButton;
	bool excludeParam_firstRun = true;
	bool excludeLearn = false;
	/** [Stored to JSON] */
	std::set<std::tuple<int, int>> excludedParams;

	/** Member fields for thread-sychronisation of function groupExcludeParam */
	bool excludeParams_invoke = false;
	std::tuple<int, int> excludeParams_tuple;

	dsp::ClockDivider lightDivider;

	Strip() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MODE_PARAM, 0, 1, 0, "Toggle left/right mode");
		configParam(ON_PARAM, 0, 1, 0, "Switch/toggle strip on");
		configParam(OFF_PARAM, 0, 1, 0, "Switch strip off");
		configParam(RAND_PARAM, 0, 1, 0, "Randomize strip");
		configParam(EXCLUDE_PARAM, 0, 1, 0, "Randomize exclusion");

		lightDivider.setDivision(1024);
		onReset();
	}

	void process(const ProcessArgs &args) override {
		if (excludeParam_firstRun) {
			groupExcludeCleanup();
			excludeParam_firstRun = false;
		}

		if (modeTrigger.process(params[MODE_PARAM].getValue())) {
			mode = (mode + 1) % 3;
			lastState = true;
		}

		if (offPTrigger.process(params[OFF_PARAM].getValue() + inputs[OFF_INPUT].getVoltage())) {
			groupDisable(true);
		}

		switch (onMode) {
			case STRIP_ONMODE_DEFAULT:
				if (onTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage()))
					groupDisable(false);
				break;
			case STRIP_ONMODE_TOGGLE:
				if (onTrigger.process(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage()))
					groupDisable(!lastState);
				break;
			case STRIP_ONMODE_HIGHLOW:
				groupDisable(params[ON_PARAM].getValue() + inputs[ON_INPUT].getVoltage() < 1.f);
				break;
		}

		if (randTrigger.process(params[RAND_PARAM].getValue() + inputs[RAND_INPUT].getVoltage())) {
			groupRandomize();
		}

		switch (excludeButton.step(params[EXCLUDE_PARAM])) {
			default:
			case LongPressButton::NO_PRESS:
				break;
			case LongPressButton::SHORT_PRESS:
				groupExcludeCleanup();
				excludeLearn ^= true;
				break;
			case LongPressButton::LONG_PRESS:
				excludedParams.clear();
				break;
		}

		if (excludeParams_invoke) {
			groupExcludeParam();
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			lights[RIGHT_LIGHT].setBrightness(mode == STRIP_MODE_LEFTRIGHT || mode == STRIP_MODE_RIGHT);
			lights[LEFT_LIGHT].setBrightness(mode == STRIP_MODE_LEFTRIGHT || mode == STRIP_MODE_LEFT);

			lights[EXCLUDE_LIGHT + 0].setBrightness(!excludeLearn && excludedParams.size() > 0 ? 1.f : 0.f);
			lights[EXCLUDE_LIGHT + 1].setBrightness(excludeLearn ? 1.f : 0.f);
		}
	}

	/** 
	 * Disables/enables all modules of the current strip.
	 * To be called from engine-thread only.
	 */
	void groupDisable(bool val) {
		if (lastState == val) return;
		lastState = val;
		if (mode == STRIP_MODE_LEFTRIGHT || mode == STRIP_MODE_RIGHT) {
			Module *m = this;
			while (true) {
				if (m->rightExpander.moduleId < 0) break;
				// This is what "Module.hpp" says about bypass:
				// "Module subclasses should not read/write this variable."
				m->rightExpander.module->bypass = val;
				// Clear outputs and set to 1 channel
				for (Output &output : m->rightExpander.module->outputs) {
					// This zeros all voltages, but the channel is set to 1 if connected
					output.setChannels(0);
				}
				m = m->rightExpander.module;
			}
		}
		if (mode == STRIP_MODE_LEFTRIGHT || mode == STRIP_MODE_LEFT) {
			Module *m = this;
			while (true) {
				if (m->leftExpander.moduleId < 0) break;
				// This is what "Module.hpp" says about bypass:
				// "Module subclasses should not read/write this variable."
				m->leftExpander.module->bypass = val;
				// Clear outputs and set to 1 channel
				for (Output &output : m->leftExpander.module->outputs) {
					// This zeros all voltages, but the channel is set to 1 if connected
					output.setChannels(0);
				}
				m = m->leftExpander.module;
			}
		}
	}

	/** 
	 * Randomizes all modules of the current strip.
	 * To be called from engine-thread only.
	 */
	void groupRandomize() {
		if (mode == STRIP_MODE_LEFTRIGHT || mode == STRIP_MODE_RIGHT) {
			Module *m = this;
			while (true) {
				if (m->rightExpander.moduleId < 0) break;
				// Be careful: this function is called from the dsp-thread, but widgets belong
				// to the app-world!
				ModuleWidget *mw = APP->scene->rack->getModule(m->rightExpander.moduleId);
				for (ParamWidget *param : mw->params) {
					if (excludedParams.find(std::make_tuple(m->rightExpander.moduleId, param->paramQuantity->paramId)) == excludedParams.end())
						param->randomize();
				}
				m->rightExpander.module->onRandomize();
				m = m->rightExpander.module;
			}
		}
		if (mode == STRIP_MODE_LEFTRIGHT || mode == STRIP_MODE_LEFT) {
			Module *m = this;
			while (true) {
				if (m->leftExpander.moduleId < 0) break;
				// Be careful: this function is called from the dsp-thread, but widgets belong
				// to the app-world!
				ModuleWidget *mw = APP->scene->rack->getModule(m->leftExpander.moduleId);
				for (ParamWidget *param : mw->params) {
					if (excludedParams.find(std::make_tuple(m->leftExpander.moduleId, param->paramQuantity->paramId)) == excludedParams.end())
						param->randomize();
				}
				m->leftExpander.module->onRandomize();
				m = m->leftExpander.module;
			}
		}
	}

	/**
	 * Adds a parameter to the randomization exclusion list.
	 * Called from the app-thread for simple synchronization.
	 */
	void groupExcludeParam(int moduleId, int paramId) {
		excludeParams_tuple = std::make_tuple(moduleId, paramId);
		excludeParams_invoke = true;
	}

	/** 
	 * Adds a parameter to the randomization exclusion list.
	 * Called from the dsp-thread to ensure thread-safe access to set excludedParams.
	 */
	void groupExcludeParam() {
		int moduleId = std::get<0>(excludeParams_tuple);
		int paramId = std::get<1>(excludeParams_tuple);

		excludeLearn = false;
		excludeParams_invoke = false;
		if (mode == STRIP_MODE_LEFTRIGHT || mode == STRIP_MODE_RIGHT) {
			Module *m = this;
			while (true) {
				if (m->rightExpander.moduleId < 0) break;
				if (m->rightExpander.moduleId == moduleId) {
					ModuleWidget *mw = APP->scene->rack->getModule(m->rightExpander.moduleId);
					for (ParamWidget *param : mw->params) {
						if (param->paramQuantity->paramId == paramId) {
							excludedParams.insert(std::make_tuple(moduleId, paramId));
							return;
						}
					}
					return;
				}
				m = m->rightExpander.module;
			}
		}
		if (mode == STRIP_MODE_LEFTRIGHT || mode == STRIP_MODE_LEFT) {
			Module *m = this;
			while (true) {
				if (m->leftExpander.moduleId < 0) break;
				if (m->leftExpander.moduleId == moduleId) {
					ModuleWidget *mw = APP->scene->rack->getModule(m->leftExpander.moduleId);
					for (ParamWidget *param : mw->params) {
						if (param->paramQuantity->paramId == paramId) {
							excludedParams.insert(std::make_tuple(moduleId, paramId));
							return;
						}
					}
					return;
				}
				m = m->leftExpander.module;
			}
		}
	}

	/**
	 * Cleans the currently list of excluded parameters from modules that are no longer 
	 * within the current strip.
	 * To be called from engine-thread only.
	 */
	void groupExcludeCleanup() {
		if (excludedParams.size() == 0)
			return;

		std::map<int, Module*> modules;
		if (mode == STRIP_MODE_LEFTRIGHT || mode == STRIP_MODE_RIGHT) {
			Module *m = this;
			while (true) {
				if (m->rightExpander.moduleId < 0) break;
				modules[m->rightExpander.moduleId] = m;
				m = m->rightExpander.module;
			}
		}
		if (mode == STRIP_MODE_LEFTRIGHT || mode == STRIP_MODE_LEFT) {
			Module *m = this;
			while (true) {
				if (m->leftExpander.moduleId < 0) break;
				modules[m->leftExpander.moduleId] = m;
				m = m->leftExpander.module;
			}
		}

		std::vector<std::tuple<int, int>> toBeDeleted;
		for (auto it : excludedParams) {
			int moduleId = std::get<0>(it);
			auto m = modules.find(moduleId);
			if (m == modules.end()) {
				toBeDeleted.push_back(it);
			}
		}

		for (auto it : toBeDeleted) {
			excludedParams.erase(it);
		}
	}


	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "mode", json_integer(mode));
		json_object_set_new(rootJ, "onMode", json_integer(onMode));

		groupExcludeCleanup();
		json_t *excludedParamsJ = json_array();
		for (auto t : excludedParams) {
			json_t *excludedParamJ = json_object();
			json_object_set_new(excludedParamJ, "moduleId", json_integer(std::get<0>(t)));
			json_object_set_new(excludedParamJ, "paramId", json_integer(std::get<1>(t)));
			json_array_append_new(excludedParamsJ, excludedParamJ);
		}
		json_object_set_new(rootJ, "excludedParams", excludedParamsJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *modeJ = json_object_get(rootJ, "mode");
		mode = json_integer_value(modeJ);
		json_t *onModeJ = json_object_get(rootJ, "onMode");
		onMode = json_integer_value(onModeJ);

		excludedParams.clear();
		json_t *excludedParamsJ = json_object_get(rootJ, "excludedParams");
		if (excludedParamsJ) {
			json_t *excludedParamJ;
			size_t i;
			json_array_foreach(excludedParamsJ, i, excludedParamJ) {
				json_t *moduleIdJ = json_object_get(excludedParamJ, "moduleId");
				json_t *paramIdJ = json_object_get(excludedParamJ, "paramId");
				if (!(moduleIdJ && paramIdJ))
					continue;

				int moduleId = json_integer_value(moduleIdJ);
				int paramId = json_integer_value(paramIdJ);
				excludedParams.insert(std::make_tuple(moduleId, paramId));
			}
		}
	}
};


struct StripOnModeMenuItem : MenuItem {
	struct StripOnModeItem : MenuItem {
		Strip *module;
		int onMode;

		void onAction(const event::Action &e) override {
			module->onMode = onMode;
		}

		void step() override {
			rightText = module->onMode == onMode ? "✔" : "";
			MenuItem::step();
		}
	};

	Strip *module;
	Menu *createChildMenu() override {
		Menu *menu = new Menu;
		menu->addChild(construct<StripOnModeItem>(&MenuItem::text, "Default", &StripOnModeItem::module, module, &StripOnModeItem::onMode, STRIP_ONMODE_DEFAULT));
		menu->addChild(construct<StripOnModeItem>(&MenuItem::text, "Toggle", &StripOnModeItem::module, module, &StripOnModeItem::onMode, STRIP_ONMODE_TOGGLE));
		menu->addChild(construct<StripOnModeItem>(&MenuItem::text, "High/Low", &StripOnModeItem::module, module, &StripOnModeItem::onMode, STRIP_ONMODE_HIGHLOW));
		return menu;
	}
};


struct ExcludeButton : TL1105 {
	Strip *module;
	bool learn = false;

	void step() override {
		if (!module)
			return;
		if (module->excludeLearn && !learn) {
			learn = true;
			APP->scene->rack->touchedParam = NULL;
		}
		TL1105::step();
	}

	void onDeselect(const event::Deselect &e) override {
		if (!module)
			return;
		if (!learn)
			return;
		// Check if a ParamWidget was touched
		ParamWidget *touchedParam = APP->scene->rack->touchedParam;
		if (touchedParam && touchedParam->paramQuantity->module != module) {
			APP->scene->rack->touchedParam = NULL;
			int moduleId = touchedParam->paramQuantity->module->id;
			int paramId = touchedParam->paramQuantity->paramId;
			// Called from the app-thread, synchronization to the dsp-thread will be done in the module.
			module->groupExcludeParam(moduleId, paramId);
			learn = false;
		}
	}

	void onButton(const event::Button &e) override {
		// Right click to open context menu
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && (e.mods & RACK_MOD_MASK) == 0) {
			createContextMenu();
			e.consume(this);
		}
		else {
			TL1105::onButton(e);
		}
	}

	void createContextMenu() {
		ui::Menu *menu = createMenu();

		ui::MenuLabel *modelLabel = new ui::MenuLabel;
		modelLabel->text = "Randomize exclusion";
		menu->addChild(modelLabel);

		struct LabelButton : ui::MenuItem {
			void onButton(const event::Button &e) override { }
		};

		LabelButton *help1Label = new LabelButton;
		help1Label->rightText = "short press";
		help1Label->text = "Learn";
		menu->addChild(help1Label);

		LabelButton *help2Label = new LabelButton;
		help2Label->rightText = "long press";
		help2Label->text = "Clear";
		menu->addChild(help2Label);

		if (module->excludedParams.size() > 0) {
			menu->addChild(new MenuSeparator());
		}

		for (auto it : module->excludedParams) {
			int moduleId = std::get<0>(it);
			int paramId = std::get<1>(it);
			
			ModuleWidget *moduleWidget = APP->scene->rack->getModule(moduleId);
			if (!moduleWidget) continue;
			ParamWidget *paramWidget = moduleWidget->getParam(paramId);
			if (!paramWidget) continue;
			
			std::string text = "Excluded \"";
			text += moduleWidget->model->name;
			text += " ";
			text += paramWidget->paramQuantity->getLabel();
			text += "\"";

			ui::MenuLabel *modelLabel = new ui::MenuLabel;
			modelLabel->text = text;
			menu->addChild(modelLabel);
		}
	}
};


struct StripWidget : ModuleWidget {
	Strip *module;
	std::string warningLog;

	StripWidget(Strip *module) {
		this->module = module;
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Strip.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<CKD6>(Vec(22.5f, 67.3f), module, Strip::MODE_PARAM));

		addChild(createLightCentered<SmallLight<GreenLight>>(Vec(16.0f, 111.f), module, Strip::LEFT_LIGHT));
		addChild(createLightCentered<SmallLight<GreenLight>>(Vec(29.0f, 111.f), module, Strip::RIGHT_LIGHT));

		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 146.7f), module, Strip::ON_INPUT));
		addParam(createParamCentered<TL1105>(Vec(22.5f, 170.1f), module, Strip::ON_PARAM));
		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 210.1f), module, Strip::OFF_INPUT));
		addParam(createParamCentered<TL1105>(Vec(22.5f, 233.5f), module, Strip::OFF_PARAM));

		addInput(createInputCentered<PJ301MPort>(Vec(22.5f, 273.1f), module, Strip::RAND_INPUT));
		addParam(createParamCentered<TL1105>(Vec(22.5f, 296.4f), module, Strip::RAND_PARAM));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(Vec(32.3f, 333.7f), module, Strip::EXCLUDE_LIGHT));
		
		ExcludeButton *button = createParamCentered<ExcludeButton>(Vec(22.5f, 324.0f), module, Strip::EXCLUDE_PARAM);
		button->module = module;
		addParam(button);
	}

	/**
	 * Removes all modules in the group. Used for "cut" in cut & paste.
	 */
	void groupRemove() {
		// Collect all modules right next to this instance of STRIP.
		std::vector<int> toBeRemoved;
		if (module->mode == STRIP_MODE_LEFTRIGHT || module->mode == STRIP_MODE_RIGHT) {
			Module *m = module;
			while (true) {
				if (m->rightExpander.moduleId < 0) break;
				toBeRemoved.push_back(m->rightExpander.moduleId);
				m = m->rightExpander.module;
			}
		}
		if (module->mode == STRIP_MODE_LEFTRIGHT || module->mode == STRIP_MODE_LEFT) {
			Module *m = module;
			while (true) {
				if (m->leftExpander.moduleId < 0) break;
				toBeRemoved.push_back(m->leftExpander.moduleId);
				m = m->leftExpander.module;
			}
		}
		for (int id : toBeRemoved) {
			ModuleWidget *mw = APP->scene->rack->getModule(id);
			APP->scene->rack->removeModule(mw);
			delete mw;
		}
	}

	/**
	 *  Make enough space directly next to this instance of STRIP for the new modules.
	 */
	void groupClearSpace(json_t *rootJ) {
		// To make sure there is enough space for the modules shove the existing modules to the 
		// left and to the right. This is done by moving this instance of STRIP stepwise 1HP until enough
		// space is cleared on both sides. Why this stupid and not just use setModulePosForce?
		// Because setModulePosForce will clear the space, but is not certain in which direction the
		// existing modules will be moved because a new big module will push a small module to its closer 
		// side. This would result to foreign modules within the loaded strip.
		if (module->mode == STRIP_MODE_LEFTRIGHT || module->mode == STRIP_MODE_RIGHT) {
			float rightWidth = json_real_value(json_object_get(rootJ, "rightWidth"));
			if (rightWidth > 0.f) {
				Vec pos = box.pos;
				for (int i = 0; i < (rightWidth / RACK_GRID_WIDTH) + 4; i++) {
					Vec np = box.pos.plus(Vec(RACK_GRID_WIDTH, 0));
					APP->scene->rack->setModulePosForce(this, np);
				}
				APP->scene->rack->setModulePosForce(this, pos);
			}
		}
		if (module->mode == STRIP_MODE_LEFTRIGHT || module->mode == STRIP_MODE_LEFT) {
			float leftWidth = json_real_value(json_object_get(rootJ, "leftWidth"));
				if (leftWidth > 0.f) {
				Vec pos = box.pos;
				for (int i = 0; i < (leftWidth / RACK_GRID_WIDTH) + 4; i++) {
					Vec np = box.pos.plus(Vec(-RACK_GRID_WIDTH, 0));
					APP->scene->rack->setModulePosForce(this, np);
				}
				APP->scene->rack->setModulePosForce(this, pos);
			}
		}
	}

	/**
	 * Creates a module from json data, also returns the previous id of the module
	 * @moduleJ
	 * @oldId
	 */
	ModuleWidget *moduleFromJson(json_t *moduleJ, int &oldId) {
		// Get slugs
		json_t *pluginSlugJ = json_object_get(moduleJ, "plugin");
		if (!pluginSlugJ) 
			return NULL;
		json_t *modelSlugJ = json_object_get(moduleJ, "model");
		if (!modelSlugJ) 
			return NULL;
		std::string pluginSlug = json_string_value(pluginSlugJ);
		std::string modelSlug = json_string_value(modelSlugJ);

		json_t *idJ = json_object_get(moduleJ, "id");
		oldId = idJ ? json_integer_value(idJ) : -1;

		// Get Model
		plugin::Model *model = plugin::getModel(pluginSlug, modelSlug);
		if (!model)
			return NULL;

		// Create ModuleWidget
		ModuleWidget *moduleWidget = model->createModuleWidget();
		assert(moduleWidget);
		return moduleWidget;
	}

	/**
	 *  Adds a new module to the rack from a json-representation.
	 * @moduleJ
	 * @left Should the module placed left or right of @box?
	 * @box
	 * @oldId
	 */
	ModuleWidget *moduleToRack(json_t *moduleJ, bool left, Rect &box, int &oldId) {
		ModuleWidget *moduleWidget = moduleFromJson(moduleJ, oldId);
		if (moduleWidget) {
			moduleWidget->box.pos = left ? box.pos.minus(Vec(moduleWidget->box.size.x, 0)) : box.pos;
			moduleWidget->module->id = -1;
			APP->scene->rack->addModule(moduleWidget);
			APP->scene->rack->setModulePosForce(moduleWidget, moduleWidget->box.pos);
			box.size = moduleWidget->box.size;
			box.pos = moduleWidget->box.pos;
			return moduleWidget;
		}
		else {
			json_t *pluginSlugJ = json_object_get(moduleJ, "plugin");
			std::string pluginSlug = json_string_value(pluginSlugJ);
			json_t *modelSlugJ = json_object_get(moduleJ, "model");
			std::string modelSlug = json_string_value(modelSlugJ);
			warningLog += string::f("Could not find module \"%s\" of plugin \"%s\"\n", modelSlug.c_str(), pluginSlug.c_str());
			box = Rect(box.pos, Vec(0, 0));
			return NULL;
		}
	}

	/**
	 * Adds modules next to this module according to the supplied json-representation.
	 * @rootJ json-representation of the STRIP-file
	 * @modules maps old module ids the new modules
	 */
	void groupFromJson_modules(json_t *rootJ, std::map<int, ModuleWidget*> &modules) {
		if (module->mode == STRIP_MODE_LEFTRIGHT || module->mode == STRIP_MODE_RIGHT) {
			Rect box = this->box;
			json_t *rightModulesJ = json_object_get(rootJ, "rightModules");
			if (rightModulesJ) {
				json_t *moduleJ;
				size_t moduleIndex;
				json_array_foreach(rightModulesJ, moduleIndex, moduleJ) {
					int oldId;
					box.pos = box.pos.plus(Vec(box.size.x, 0));
					ModuleWidget *mw = moduleToRack(moduleJ, false, box, oldId);
					// mw could be NULL, just move on
					modules[oldId] = mw;
				}
			}
		}
		if (module->mode == STRIP_MODE_LEFTRIGHT || module->mode == STRIP_MODE_LEFT) {
			Rect box = this->box;
			json_t *leftModulesJ = json_object_get(rootJ, "leftModules");
			if (leftModulesJ) {
				json_t *moduleJ;
				size_t moduleIndex;
				json_array_foreach(leftModulesJ, moduleIndex, moduleJ) {
					int oldId;
					ModuleWidget *mw = moduleToRack(moduleJ, true, box, oldId);
					modules[oldId] = mw;
				}
			}
		}
	}

	/**
	 * Fixes parameter mappings within a preset. This can be considered a hack because
	 * Rack v1 offers no API for reading the mapping module of a parameter. So this replaces the
	 * module id in the preset JSON with the new module id to preserve correct mapping.
	 * This means every module using mapping must be handled explicitly.
	 * @moduleJ json-representation of the module
	 * @modules maps old module ids the new modules
	 */
	void groupFromJson_presets_fixMapping(json_t *moduleJ, std::map<int, ModuleWidget*> &modules) {
		std::string pluginSlug = json_string_value(json_object_get(moduleJ, "plugin"));
		std::string modelSlug = json_string_value(json_object_get(moduleJ, "model"));

		// Only handle some specific modules known to use mapping of parameters
		if (!( (pluginSlug == "Stoermelder-P1" && (modelSlug == "CVMap" || modelSlug == "CVMapMicro" || modelSlug == "CVPam" || modelSlug == "ReMoveLite"))
			|| (pluginSlug == "Core" && modelSlug == "MIDI-Map"))) 
			return;

		json_t *dataJ = json_object_get(moduleJ, "data");
		json_t *mapsJ = json_object_get(dataJ, "maps");
		if (mapsJ) {
			json_t *mapJ;
			size_t mapIndex;
			json_array_foreach(mapsJ, mapIndex, mapJ) {
				json_t *moduleIdJ = json_object_get(mapJ, "moduleId");
				if (!moduleIdJ)
					continue;
				int oldId = json_integer_value(moduleIdJ);
				if (oldId >= 0) {
					int newId = -1;
					ModuleWidget *mw = modules[oldId];
					if (mw != NULL) {
						newId = mw->module->id;
					}
					json_object_set_new(mapJ, "moduleId", json_integer(newId));
				}
			}
		}
	}

	/**
	 * Loads all the presets from a json-representation generated by STRIP. Assumes the modules are there.
	 * Presets of non-existing modules will be skipped.
	 * @json json-representation of the STRIP-file
	 * @modules maps old module ids the new modules
	 */
	void groupFromJson_presets(json_t *rootJ, std::map<int, ModuleWidget*> &modules) {
		json_t *rightModulesJ = json_object_get(rootJ, "rightModules");
		if (rightModulesJ) {
			json_t *moduleJ;
			size_t moduleIndex;
			json_array_foreach(rightModulesJ, moduleIndex, moduleJ) {
				if (module->mode == STRIP_MODE_LEFTRIGHT || module->mode == STRIP_MODE_RIGHT) {
					groupFromJson_presets_fixMapping(moduleJ, modules);
					int oldId = json_integer_value(json_object_get(moduleJ, "id"));
					ModuleWidget *mw = modules[oldId];
					if (mw != NULL) {
						mw->fromJson(moduleJ);
					}
				}
			}
		}
		json_t *leftModulesJ = json_object_get(rootJ, "leftModules");
		if (leftModulesJ) {
			json_t *moduleJ;
			size_t moduleIndex;
			json_array_foreach(leftModulesJ, moduleIndex, moduleJ) {
				if (module->mode == STRIP_MODE_LEFTRIGHT || module->mode == STRIP_MODE_LEFT) {
					groupFromJson_presets_fixMapping(moduleJ, modules);
					int oldId = json_integer_value(json_object_get(moduleJ, "id"));
					ModuleWidget *mw = modules[oldId];
					if (mw != NULL) {
						mw->fromJson(moduleJ);
					}
				}
			}
		}
	}

	/**
	 * Adds cables loaded from a json-representation generated by STRIP.
	 * If a module is missing the cable will be obviously skipped.
	 * @rootJ json-representation of the STRIP-file
	 * @modules maps old module ids the new modules
	 */
	void groupFromJson_cables(json_t *rootJ, std::map<int, ModuleWidget*> &modules) {
		json_t *cablesJ = json_object_get(rootJ, "cables");
		if (cablesJ) {
			json_t *cableJ;
			size_t cableIndex;
			json_array_foreach(cablesJ, cableIndex, cableJ) {
				int outputModuleId = json_integer_value(json_object_get(cableJ, "outputModuleId"));
				int outputId = json_integer_value(json_object_get(cableJ, "outputId"));
				int inputModuleId = json_integer_value(json_object_get(cableJ, "inputModuleId"));
				int inputId = json_integer_value(json_object_get(cableJ, "inputId"));
				const char *colorStr = json_string_value(json_object_get(cableJ, "color"));

				ModuleWidget *outputModule = modules[outputModuleId];
				ModuleWidget *inputModule = modules[inputModuleId];
				// In case one of the modules could not be loaded
				if (!outputModule || !inputModule) continue;

				CableWidget *cw = new CableWidget;
				if (colorStr) {
					cw->color = color::fromHexString(colorStr);
				}
				for (PortWidget *port : outputModule->outputs) {
					if (port->portId == outputId) {
						cw->setOutput(port);
						break;
					}
				}
				for (PortWidget *port : inputModule->inputs) {
					if (port->portId == inputId) {
						cw->setInput(port);
						break;
					}
				}
				if (cw->isComplete()) {
					APP->scene->rack->addCable(cw);
				}
			}
		}
	}


	void groupToJson(json_t *rootJ) {
		// Add modules
		std::set<ModuleWidget*> modules;
		
		float rightWidth = 0.f;
		json_t *rightModulesJ = json_array();
		if (module->mode == STRIP_MODE_LEFTRIGHT || module->mode == STRIP_MODE_RIGHT) {
			Module *m = module;
			while (true) {
				if (m->rightExpander.moduleId < 0) break;
				ModuleWidget *mw = APP->scene->rack->getModule(m->rightExpander.moduleId);
				json_t *moduleJ = mw->toJson();
				assert(moduleJ);
				json_array_append_new(rightModulesJ, moduleJ);
				modules.insert(mw);
				rightWidth += mw->box.size.x;
				m = m->rightExpander.module;
			}
		}

		float leftWidth = 0.f;
		json_t *leftModulesJ = json_array();
		if (module->mode == STRIP_MODE_LEFTRIGHT || module->mode == STRIP_MODE_LEFT) {
			Module *m = module;
			while (true) {
				if (m->leftExpander.moduleId < 0) break;
				ModuleWidget *mw = APP->scene->rack->getModule(m->leftExpander.moduleId);
				json_t *moduleJ = mw->toJson();
				assert(moduleJ);
				json_array_append_new(leftModulesJ, moduleJ);
				modules.insert(mw);
				leftWidth += mw->box.size.x;
				m = m->leftExpander.module;
			}
		}

		// Add cables
		json_t *cablesJ = json_array();
		for (auto i = modules.begin(); i != modules.end(); ++i) {
			ModuleWidget *outputModule = *i;
			// It is enough to check the outputs, as inputs don't matter when the other end is outside of the group
			for (PortWidget* output : outputModule->outputs) {
				for (CableWidget *cw : APP->scene->rack->getCablesOnPort(output)) {
					if (!cw->isComplete()) 
						continue;

					PortWidget* input = cw->inputPort;
					ModuleWidget *inputModule = APP->scene->rack->getModule(input->module->id);
					if (modules.find(inputModule) == modules.end()) 
						continue;

					std::string colorStr = color::toHexString(cw->color);

					json_t *cableJ = json_object();
					json_object_set_new(cableJ, "outputModuleId", json_integer(output->module->id));
					json_object_set_new(cableJ, "outputId", json_integer(output->portId));
					json_object_set_new(cableJ, "inputModuleId", json_integer(input->module->id));
					json_object_set_new(cableJ, "inputId", json_integer(input->portId));
					json_object_set_new(cableJ, "color", json_string(colorStr.c_str()));
					json_array_append_new(cablesJ, cableJ);
				}
			}
		}

		json_object_set_new(rootJ, "stripVersion", json_integer(1));
		json_object_set_new(rootJ, "rightModules", rightModulesJ);
		json_object_set_new(rootJ, "rightWidth", json_real(rightWidth));
		json_object_set_new(rootJ, "leftModules", leftModulesJ);
		json_object_set_new(rootJ, "leftWidth", json_real(leftWidth));
		json_object_set_new(rootJ, "cables", cablesJ);

		json_t *versionJ = json_string(app::APP_VERSION.c_str());
		json_object_set_new(rootJ, "version", versionJ);
	}

	void groupCopyClipboard() {
		json_t *rootJ = json_object();
		groupToJson(rootJ);

		DEFER({
			json_decref(rootJ);
		});
		char *moduleJson = json_dumps(rootJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		DEFER({
			free(moduleJson);
		});
		glfwSetClipboardString(APP->window->win, moduleJson);
	}

	void groupCutClipboard() {
		json_t *rootJ = json_object();
		groupToJson(rootJ);

		DEFER({
			json_decref(rootJ);
		});
		char *moduleJson = json_dumps(rootJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		DEFER({
			free(moduleJson);
		});
		glfwSetClipboardString(APP->window->win, moduleJson);
		groupRemove();
	}

	void groupSaveFile(std::string filename) {
		INFO("Saving preset %s", filename.c_str());

		json_t *rootJ = json_object();
		groupToJson(rootJ);

		DEFER({
			json_decref(rootJ);
		});

		FILE *file = fopen(filename.c_str(), "w");
		if (!file) {
			std::string message = string::f("Could not write to patch file %s", filename.c_str());
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
		}
		DEFER({
			fclose(file);
		});

		json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
	}

	void groupSaveFileDialog() {
		osdialog_filters *filters = osdialog_filters_parse(PRESET_FILTERS);
		DEFER({
			osdialog_filters_free(filters);
		});

		char *path = osdialog_file(OSDIALOG_SAVE, "", "Untitled.vcvss", filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		std::string pathStr = path;
		std::string extension = string::filenameExtension(string::filename(pathStr));
		if (extension.empty()) {
			pathStr += ".vcvss";
		}

		groupSaveFile(pathStr);
	}

	void groupFromJson(json_t *rootJ) {
		warningLog = "";

		// Clear modules next to STRIP
		groupClearSpace(rootJ);

		// Maps old moduleId to the newly created module (with new id)
		std::map<int, ModuleWidget*> modules;
		// Add modules
		groupFromJson_modules(rootJ, modules);
		// Load presets for modules, also fixes parameter mappings
		groupFromJson_presets(rootJ, modules);

		// Add cables
		groupFromJson_cables(rootJ, modules);

		// Does nothing, but fixes https://github.com/VCVRack/Rack/issues/1444 for Rack <= 1.1.1
		APP->scene->rack->requestModulePos(this, this->box.pos);

		if (!warningLog.empty()) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, warningLog.c_str());
		}
	}

	void groupPasteClipboard() {
		const char *moduleJson = glfwGetClipboardString(APP->window->win);
		if (!moduleJson) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Could not get text from clipboard.");
			return;
		}

		json_error_t error;
		json_t *rootJ = json_loads(moduleJson, 0, &error);
		if (!rootJ) {
			std::string message = string::f("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		groupFromJson(rootJ);
	}

	void groupLoadFile(std::string filename) {
		INFO("Loading preset %s", filename.c_str());

		FILE *file = fopen(filename.c_str(), "r");
		if (!file) {
			std::string message = string::f("Could not load file %s", filename.c_str());
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			fclose(file);
		});

		json_error_t error;
		json_t *rootJ = json_loadf(file, 0, &error);
		if (!rootJ) {
			std::string message = string::f("File is not a valid file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
			return;
		}
		DEFER({
			json_decref(rootJ);
		});

		groupFromJson(rootJ);
	}

	void groupLoadFileDialog() {
		osdialog_filters *filters = osdialog_filters_parse(PRESET_FILTERS);
		DEFER({
			osdialog_filters_free(filters);
		});

		char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, filters);
		if (!path) {
			// No path selected
			return;
		}
		DEFER({
			free(path);
		});

		groupLoadFile(path);
	}


	void onHoverKey(const event::HoverKey &e) override {
		ModuleWidget::onHoverKey(e);
		if (e.isConsumed())
			return;

		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			switch (e.key) {
				case GLFW_KEY_C: {
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
						groupCopyClipboard();
						e.consume(this);
					}
				} break;
				case GLFW_KEY_V: {
					if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
						groupPasteClipboard();
						e.consume(this);
					}
				} break;
			}
		}
	}

	void appendContextMenu(Menu *menu) override {
		Strip *module = dynamic_cast<Strip*>(this->module);
		assert(module);

		struct ManualItem : MenuItem {
			void onAction(const event::Action &e) override {
				std::thread t(system::openBrowser, "https://github.com/stoermelder/vcvrack-packone/blob/v1/docs/Strip.md");
				t.detach();
			}
		};

		menu->addChild(construct<ManualItem>(&MenuItem::text, "Module Manual"));
		menu->addChild(new MenuSeparator());

		StripOnModeMenuItem *stripOnModeMenuItem = construct<StripOnModeMenuItem>(&MenuItem::text, "Port/Switch ON mode", &StripOnModeMenuItem::module, module);
		stripOnModeMenuItem->rightText = RIGHT_ARROW;
		menu->addChild(stripOnModeMenuItem);
		menu->addChild(new MenuSeparator());

		struct CutGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->groupCutClipboard();
			}
		};

		struct CopyGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->groupCopyClipboard();
			}
		};

		struct PasteGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->groupPasteClipboard();
			}
		};

		struct LoadGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->groupLoadFileDialog();
			}
		};

		struct SaveGroupMenuItem : MenuItem {
			StripWidget *moduleWidget;

			void onAction(const event::Action &e) override {
				moduleWidget->groupSaveFileDialog();
			}
		};

		ui::MenuLabel *modelLabel = new ui::MenuLabel;
		modelLabel->text = "Strip";
		menu->addChild(modelLabel);

		CutGroupMenuItem *cutGroupMenuItem = construct<CutGroupMenuItem>(&MenuItem::text, "Cut", &CutGroupMenuItem::moduleWidget, this);
		menu->addChild(cutGroupMenuItem);
		CopyGroupMenuItem *copyGroupMenuItem = construct<CopyGroupMenuItem>(&MenuItem::text, "Copy", &MenuItem::rightText, "Shift+C", &CopyGroupMenuItem::moduleWidget, this);
		menu->addChild(copyGroupMenuItem);
		PasteGroupMenuItem *pasteGroupMenuItem = construct<PasteGroupMenuItem>(&MenuItem::text, "Paste", &MenuItem::rightText, "Shift+V", &PasteGroupMenuItem::moduleWidget, this);
		menu->addChild(pasteGroupMenuItem);
		LoadGroupMenuItem *loadGroupMenuItem = construct<LoadGroupMenuItem>(&MenuItem::text, "Load", &LoadGroupMenuItem::moduleWidget, this);
		menu->addChild(loadGroupMenuItem);
		SaveGroupMenuItem *saveGroupMenuItem = construct<SaveGroupMenuItem>(&MenuItem::text, "Save as", &SaveGroupMenuItem::moduleWidget, this);
		menu->addChild(saveGroupMenuItem);
	}
};


Model *modelStrip = createModel<Strip, StripWidget>("Strip");