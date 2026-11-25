#include "plugin.hpp"

struct Attenuverter : Module {
	enum ParamId { LEVEL_PARAM, OFFSET_PARAM, PARAMS_LEN };
	enum InputId { IN_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	Attenuverter() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(LEVEL_PARAM, -1.f, 1.f, 1.f, "Level", "%", 0.f, 100.f);
		configParam(OFFSET_PARAM, -10.f, 10.f, 0.f, "Offset", " V");
		configInput(IN_INPUT, "Signal");
		configOutput(OUT_OUTPUT, "Signal");
		configBypass(IN_INPUT, OUT_OUTPUT);
	}

	void process(const ProcessArgs& args) override {
		float level = params[LEVEL_PARAM].getValue();
		float offset = params[OFFSET_PARAM].getValue();
		int channels = std::max(1, inputs[IN_INPUT].getChannels());

		for (int c = 0; c < channels; c++) {
			float v = inputs[IN_INPUT].getPolyVoltage(c) * level + offset;
			outputs[OUT_OUTPUT].setVoltage(clamp(v, -12.f, 12.f), c);
		}
		outputs[OUT_OUTPUT].setChannels(channels);
	}
};

struct AttenuverterWidget : ModuleWidget {
	AttenuverterWidget(Attenuverter* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Attenuverter.svg")));

		addChild(createWidget<ScrewSilver>(Vec(0, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(7.62, 30.0)), module, Attenuverter::LEVEL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(7.62, 55.0)), module, Attenuverter::OFFSET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.62, 85.0)), module, Attenuverter::IN_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.62, 105.0)), module, Attenuverter::OUT_OUTPUT));
	}
};

Model* modelAttenuverter = createModel<Attenuverter, AttenuverterWidget>("Attenuverter");
