#pragma once
#include "plugin.hpp"
#include "settings.hpp"
#include <chrono>


struct ParamHandleIndicator {
	ParamHandle *handle;
	NVGcolor color;
		
	int indicateCount = 0;
	float sampletime;

	void process(float sampleTime) {
		if (indicateCount > 0) {
			this->sampletime += sampleTime;
			if (this->sampletime > 0.2f) {
				this->sampletime = 0;
				indicateCount--;
				handle->color = indicateCount % 2 == 1 ? nvgRGB(0x0, 0x0, 0x0) : color;
			}
		}
	}

	void indicate() {
		indicateCount = 20;
		color = handle->color;
	}	
};


template< int MAX_CHANNELS >
struct MapModule : Module {
	/** Number of maps */
	int mapLen = 0;
	/** The mapped param handle of each channel */
	ParamHandle paramHandles[MAX_CHANNELS];
	ParamHandleIndicator paramHandleIndicator[MAX_CHANNELS];

    /** Channel ID of the learning session */
	int learningId;
	/** Whether the param has been set during the learning session */
	bool learnedParam;

	bool textScrolling = true;

    /** The smoothing processor (normalized between 0 and 1) of each channel */
	dsp::ExponentialFilter valueFilters[MAX_CHANNELS];

	dsp::ClockDivider indicatorDivider;

	MapModule() {
		for (int id = 0; id < MAX_CHANNELS; id++) {
			paramHandles[id].color = nvgRGB(0x0, 0x0, 0x0);
			paramHandleIndicator[id].handle = &paramHandles[id];
			APP->engine->addParamHandle(&paramHandles[id]);
		}
		indicatorDivider.setDivision(1024);
	}

	~MapModule() {
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->removeParamHandle(&paramHandles[id]);
		}
	}

	void onReset() override {
		learningId = -1;
		learnedParam = false;
		clearMaps();
		mapLen = 1;
	}

	void process(const ProcessArgs &args) override {
		if (indicatorDivider.process()) {
			float t = indicatorDivider.getDivision() * args.sampleTime;
			for (size_t i = 0; i < MAX_CHANNELS; i++) {
				if (paramHandles[i].moduleId >= 0)
					paramHandleIndicator[i].process(t);
			}
		}
	}

	ParamQuantity *getParamQuantity(int id) {
		// Get Module
		Module *module = paramHandles[id].module;
		if (!module)
			return NULL;
		// Get ParamQuantity
		int paramId = paramHandles[id].paramId;
		ParamQuantity *paramQuantity = module->paramQuantities[paramId];
		if (!paramQuantity)
			return NULL;
		if (!paramQuantity->isBounded())
			return NULL;
		return paramQuantity;
	}

   	virtual void clearMap(int id) {
		learningId = -1;
		APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
		valueFilters[id].reset();
		updateMapLen();
	}

	virtual void clearMaps() {
		learningId = -1;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			APP->engine->updateParamHandle(&paramHandles[id], -1, 0, true);
			valueFilters[id].reset();
		}
		mapLen = 0;
	}

	virtual void updateMapLen() {
		// Find last nonempty map
		int id;
		for (id = MAX_CHANNELS - 1; id >= 0; id--) {
			if (paramHandles[id].moduleId >= 0)
				break;
		}
		mapLen = id + 1;
		// Add an empty "Mapping..." slot
		if (mapLen < MAX_CHANNELS)
			mapLen++;
	}

	virtual void commitLearn() {
		if (learningId < 0)
			return;
		if (!learnedParam)
			return;
		// Reset learned state
		learnedParam = false;
		// Find next incomplete map
		while (++learningId < MAX_CHANNELS) {
			if (paramHandles[learningId].moduleId < 0)
				return;
		}
		learningId = -1;
	}

	virtual void enableLearn(int id) {
		if (learningId != id) {
			learningId = id;
			learnedParam = false;
		}
	}

	virtual void disableLearn(int id) {
		if (learningId == id) {
			learningId = -1;
		}
	}

	virtual void learnParam(int id, int moduleId, int paramId) {
		APP->engine->updateParamHandle(&paramHandles[id], moduleId, paramId, true);
		learnedParam = true;
		commitLearn();
		updateMapLen();
	}

 
	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "textScrolling", json_boolean(textScrolling));

		json_t *mapsJ = json_array();
		for (int id = 0; id < mapLen; id++) {
			json_t *mapJ = json_object();
			json_object_set_new(mapJ, "moduleId", json_integer(paramHandles[id].moduleId));
			json_object_set_new(mapJ, "paramId", json_integer(paramHandles[id].paramId));
			json_array_append_new(mapsJ, mapJ);
		}
		json_object_set_new(rootJ, "maps", mapsJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		clearMaps();

		json_t *textScrollingJ = json_object_get(rootJ, "textScrolling");
		textScrolling = json_boolean_value(textScrollingJ);

		json_t *mapsJ = json_object_get(rootJ, "maps");
		if (mapsJ) {
			json_t *mapJ;
			size_t mapIndex;
			json_array_foreach(mapsJ, mapIndex, mapJ) {
				json_t *moduleIdJ = json_object_get(mapJ, "moduleId");
				json_t *paramIdJ = json_object_get(mapJ, "paramId");
				if (!(moduleIdJ && paramIdJ))
					continue;
				if (mapIndex >= MAX_CHANNELS)
					continue;
				APP->engine->updateParamHandle(&paramHandles[mapIndex], json_integer_value(moduleIdJ), json_integer_value(paramIdJ), false);
			}
		}
		updateMapLen();
	}
};


template< int MAX_CHANNELS, typename MODULE >
struct MapModuleChoice : LedDisplayChoice {
	MODULE *module;
	int id;

	std::chrono::time_point<std::chrono::system_clock> hscrollUpdate = std::chrono::system_clock::now();
	int hscrollCharOffset = 0;

	MapModuleChoice() {
		box.size = mm2px(Vec(0, 7.5));
		textOffset = Vec(6, 14.7);
	}

	void setModule(MODULE *module) {
		this->module = module;
	}

	void onButton(const event::Button &e) override {
		e.stopPropagating();
		if (!module)
			return;

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
		}

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			e.consume(this);

			if (module->paramHandles[id].moduleId >= 0) {
				ui::Menu *menu = createMenu();
				std::string header = "Parameter \"" + getParamName() + "\"";
				menu->addChild(createMenuLabel(header));

				struct UnmapItem : MenuItem {
					MODULE *module;
					int id;
					void onAction(const event::Action &e) override {
						module->clearMap(id);
					}
				};
				menu->addChild(construct<UnmapItem>(&MenuItem::text, "Unmap", &UnmapItem::module, module, &UnmapItem::id, id));

				struct IndicateItem : MenuItem {
					MODULE *module;
					int id;
					void onAction(const event::Action &e) override {
						ParamHandle *paramHandle = &module->paramHandles[id];
						ModuleWidget *mw = APP->scene->rack->getModule(paramHandle->moduleId);
						if (mw) {
							// move the view to center the mapped module
							// NB: unstable API!
							Vec offset = mw->box.pos;
							offset = offset.plus(mw->box.size.mult(0.5f));
							offset = offset.mult(APP->scene->rackScroll->zoomWidget->zoom);
							offset = offset.minus(APP->scene->box.size.mult(0.5f));
							APP->scene->rackScroll->offset = offset;
							rack::settings::zoom = 1.f;
						}					
						module->paramHandleIndicator[id].indicate();
					}
				};
				menu->addChild(construct<IndicateItem>(&MenuItem::text, "Locate and indicate", &IndicateItem::module, module, &IndicateItem::id, id));
				appendContextMenu(menu);
			} 
			else {
				module->clearMap(id);
			}
		}
	}

	virtual void appendContextMenu(Menu *menu) {
	}

	void onSelect(const event::Select &e) override {
		if (!module)
			return;

		ScrollWidget *scroll = getAncestorOfType<ScrollWidget>();
		scroll->scrollTo(box);

		// Reset touchedParam
		APP->scene->rack->touchedParam = NULL;
		module->enableLearn(id);
	}

	void onDeselect(const event::Deselect &e) override {
		if (!module)
			return;
		// Check if a ParamWidget was touched
		ParamWidget *touchedParam = APP->scene->rack->touchedParam;
		if (touchedParam && touchedParam->paramQuantity->module != module) {
			APP->scene->rack->touchedParam = NULL;
			int moduleId = touchedParam->paramQuantity->module->id;
			int paramId = touchedParam->paramQuantity->paramId;
			module->learnParam(id, moduleId, paramId);
			hscrollCharOffset = 0;
		} 
		else {
			module->disableLearn(id);
		}
	}

	void step() override {
		if (!module)
			return;

		// Set bgColor and selected state
		if (module->learningId == id) {
			bgColor = color;
			bgColor.a = 0.15;

			// HACK
			if (APP->event->getSelectedWidget() != this)
				APP->event->setSelected(this);
		} 
		else {
			bgColor = nvgRGBA(0, 0, 0, 0);

			// HACK
			if (APP->event->getSelectedWidget() == this)
				APP->event->setSelected(NULL);
		}

		// Set text
		text = getTextPrefix();
		if (module->paramHandles[id].moduleId >= 0 && module->learningId != id) {
			std::string pn = getParamName();
			if (pn == "") {
				module->clearMap(id);
				return;
			}

			size_t hscrollMaxLength = ceil(box.size.x / 6.2f);
			if (module->textScrolling && pn.length() + text.length() > hscrollMaxLength) {
				// Scroll the parameter-name horizontically
				text += pn.substr(hscrollCharOffset > (int)pn.length() ? 0 : hscrollCharOffset);
				auto now = std::chrono::system_clock::now();
				if (now - hscrollUpdate > std::chrono::milliseconds{100}) {
					hscrollCharOffset = (hscrollCharOffset + 1) % (pn.length() + hscrollMaxLength);
					hscrollUpdate = now;
				}
			} 
			else {
				text += pn;
			}
		} 
		else {
			if (module->learningId == id) {
				text += "Mapping...";
			} else {
				text += "Unmapped";
			}
		}

		// Set text color
		if (module->paramHandles[id].moduleId >= 0 || module->learningId == id) {
			color.a = 1.0;
		} 
		else {
			color.a = 0.5;
		}
	}

	virtual std::string getTextPrefix() {
		return MAX_CHANNELS > 1 ? string::f("%02d ", id + 1) : "";
	}

	std::string getParamName() {
		if (!module)
			return "";
		if (id >= module->mapLen)
			return "";
		ParamHandle *paramHandle = &module->paramHandles[id];
		if (paramHandle->moduleId < 0)
			return "";
		ModuleWidget *mw = APP->scene->rack->getModule(paramHandle->moduleId);
		if (!mw)
			return "";
		// Get the Module from the ModuleWidget instead of the ParamHandle.
		// I think this is more elegant since this method is called in the app world instead of the engine world.
		Module *m = mw->module;
		if (!m)
			return "";
		int paramId = paramHandle->paramId;
		if (paramId >= (int) m->params.size())
			return "";
		ParamQuantity *paramQuantity = m->paramQuantities[paramId];
		std::string s;
		s += mw->model->name;
		s += " ";
		s += paramQuantity->label;
		return s;
	}
};

template< int MAX_CHANNELS, typename MODULE, typename CHOICE = MapModuleChoice<MAX_CHANNELS, MODULE> >
struct MapModuleDisplay : LedDisplay {
	MODULE *module;
	ScrollWidget *scroll;
	CHOICE *choices[MAX_CHANNELS];
	LedDisplaySeparator *separators[MAX_CHANNELS];

	void setModule(MODULE *module) {
		this->module = module;

		scroll = new ScrollWidget;
		scroll->box.size.x = box.size.x;
		scroll->box.size.y = box.size.y - scroll->box.pos.y;
		addChild(scroll);

		LedDisplaySeparator *separator = createWidget<LedDisplaySeparator>(scroll->box.pos);
		separator->box.size.x = box.size.x;
		addChild(separator);
		separators[0] = separator;

		Vec pos;
		for (int id = 0; id < MAX_CHANNELS; id++) {
			if (id > 0) {
				LedDisplaySeparator *separator = createWidget<LedDisplaySeparator>(pos);
				separator->box.size.x = box.size.x;
				scroll->container->addChild(separator);
				separators[id] = separator;
			}

			CHOICE *choice = createWidget<CHOICE>(pos);
			choice->box.size.x = box.size.x;
			choice->id = id;
			choice->setModule(module);
			scroll->container->addChild(choice);
			choices[id] = choice;

			pos = choice->box.getBottomLeft();
		}
	}
};
