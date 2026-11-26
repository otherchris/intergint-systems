#include "plugin.hpp"

static const int STEPS_PER_BEAT = 4;  // 16th notes
static const int MAX_BARS = 16;
static const int MAX_BEATS_PER_BAR = 8;
static const int MAX_STEPS = MAX_BARS * MAX_BEATS_PER_BAR * STEPS_PER_BEAT;
static const int NUM_PITCHES = 24;  // row 0 = C3, row 12 = C4 (0 V), row 23 = B4
static const int MAX_VISIBLE_STEPS = 64;  // the display shows whole bars up to this many columns
static const int MAX_VOICES = 4;

static const NVGcolor VOICE_COLORS[MAX_VOICES] = {
	nvgRGB(0xf0, 0x9a, 0x30),  // orange
	nvgRGB(0x3c, 0xc8, 0xe6),  // cyan
	nvgRGB(0xe8, 0x50, 0x9a),  // pink
	nvgRGB(0x9c, 0xcf, 0x45),  // green
};

// Scale shapes as semitone sets relative to the root. `id` is what patches
// store, so never change an existing id.
struct Scale {
	const char* id;
	const char* name;
	std::vector<int> degrees;
};

static const std::vector<Scale> SCALES = {
	{"chromatic", "Chromatic", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
	{"major", "Major (Ionian)", {0, 2, 4, 5, 7, 9, 11}},
	{"minor", "Natural minor (Aeolian)", {0, 2, 3, 5, 7, 8, 10}},
	{"harmonicMinor", "Harmonic minor", {0, 2, 3, 5, 7, 8, 11}},
	{"melodicMinor", "Melodic minor", {0, 2, 3, 5, 7, 9, 11}},
	{"dorian", "Dorian", {0, 2, 3, 5, 7, 9, 10}},
	{"phrygian", "Phrygian", {0, 1, 3, 5, 7, 8, 10}},
	{"lydian", "Lydian", {0, 2, 4, 6, 7, 9, 11}},
	{"mixolydian", "Mixolydian", {0, 2, 4, 5, 7, 9, 10}},
	{"locrian", "Locrian", {0, 1, 3, 5, 6, 8, 10}},
	{"majorPentatonic", "Major pentatonic", {0, 2, 4, 7, 9}},
	{"minorPentatonic", "Minor pentatonic", {0, 3, 5, 7, 10}},
	{"blues", "Blues", {0, 3, 5, 6, 7, 10}},
	{"wholeTone", "Whole tone", {0, 2, 4, 6, 8, 10}},
};

static const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Monophonic pattern: each step is a rest (-1) or a pitch. `tie` marks a step
// that continues the note from the previous step instead of starting a new one.
// Storage always covers MAX_STEPS, so shrinking the loop doesn't lose notes.
struct Pattern {
	int8_t pitch[MAX_STEPS];
	bool tie[MAX_STEPS];

	void clear() {
		for (int i = 0; i < MAX_STEPS; i++) {
			pitch[i] = -1;
			tie[i] = false;
		}
	}

	int noteStart(int s) const {
		while (s > 0 && tie[s]) s--;
		return s;
	}

	int noteEnd(int s, int limit = MAX_STEPS) const {
		while (s + 1 < limit && tie[s + 1]) s++;
		return s;
	}

	void deleteNote(int s) {
		int a = noteStart(s), b = noteEnd(s);
		for (int i = a; i <= b; i++) {
			pitch[i] = -1;
			tie[i] = false;
		}
	}

	// Write a note covering steps [a, b], overwriting anything there.
	// A note we cut into from the left keeps its head as a shorter note.
	void setNote(int a, int b, int p) {
		for (int i = a; i <= b; i++) {
			pitch[i] = p;
			tie[i] = (i > a);
		}
		// Whatever remains of a note we cut into on the right starts fresh.
		if (b + 1 < MAX_STEPS && tie[b + 1])
			tie[b + 1] = false;
	}
};

struct PianoRoll : Module {
	enum ParamId { ENUMS(OCTAVE_PARAMS, MAX_VOICES), ENUMS(VOICE_PARAMS, MAX_VOICES), PARAMS_LEN };
	enum InputId { CLOCK_INPUT, RESET_INPUT, INPUTS_LEN };
	enum OutputId { VOCT_OUTPUT, GATE_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(VOICE_LIGHTS, MAX_VOICES), LIGHTS_LEN };

	// Each voice is an independent monophonic lane, output on its own
	// polyphony channel.
	struct Voice {
		Pattern pattern;
		float cv = 0.f;
		bool noteOn = false;
		dsp::PulseGenerator retrigger;  // brief gate drop between adjacent notes
	};

	Voice voices[MAX_VOICES];
	int numVoices = 1;
	int activeVoice = 0;  // the voice being edited
	int bars = 4;
	int beatsPerBar = 4;
	int scale = 0;  // index into SCALES
	int root = 0;   // 0 = C ... 11 = B
	int step = -1;  // -1 = waiting for the first clock after reset

	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::PulseGenerator resetHoldoff;  // ignore a clock arriving with reset
	dsp::BooleanTrigger voiceButtons[MAX_VOICES];
	dsp::ClockDivider lightDivider;

	PianoRoll() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int v = 0; v < MAX_VOICES; v++) {
			configParam(OCTAVE_PARAMS + v, -3.f, 3.f, 0.f, string::f("Voice %d octave", v + 1));
			getParamQuantity(OCTAVE_PARAMS + v)->snapEnabled = true;
		}
		for (int v = 0; v < MAX_VOICES; v++)
			configButton(VOICE_PARAMS + v, string::f("Edit voice %d", v + 1));
		configInput(CLOCK_INPUT, "Clock (one 16th-note step per pulse)");
		configInput(RESET_INPUT, "Reset");
		configOutput(VOCT_OUTPUT, "Pitch (1V/oct, one channel per voice)");
		configOutput(GATE_OUTPUT, "Gate (one channel per voice)");
		for (Voice& v : voices)
			v.pattern.clear();
		lightDivider.setDivision(256);
	}

	Pattern& activePattern() { return voices[activeVoice].pattern; }

	void setNumVoices(int n) {
		numVoices = clamp(n, 1, MAX_VOICES);
		activeVoice = std::min(activeVoice, numVoices - 1);
	}

	int stepsPerBar() const { return beatsPerBar * STEPS_PER_BEAT; }
	int numSteps() const { return bars * stepsPerBar(); }

	bool inScale(int p) const {
		int degree = ((p - root) % 12 + 12) % 12;
		for (int d : SCALES[scale].degrees)
			if (d == degree)
				return true;
		return false;
	}

	// Nearest in-scale pitch, preferring the lower one on a tie.
	int snapToScale(int p) const {
		for (int d = 0; d < 12; d++) {
			if (inScale(p - d))
				return p - d;
			if (inScale(p + d))
				return p + d;
		}
		return p;
	}

	void onReset() override {
		for (Voice& v : voices) {
			v.pattern.clear();
			v.noteOn = false;
		}
		numVoices = 1;
		activeVoice = 0;
		bars = 4;
		beatsPerBar = 4;
		scale = 0;
		root = 0;
		step = -1;
	}

	void process(const ProcessArgs& args) override {
		// Pressing a voice button selects it, adding voices up to it if needed.
		for (int v = 0; v < MAX_VOICES; v++) {
			if (voiceButtons[v].process(params[VOICE_PARAMS + v].getValue() > 0.f)) {
				if (v >= numVoices)
					numVoices = v + 1;
				activeVoice = v;
			}
		}

		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			step = -1;
			for (Voice& v : voices)
				v.noteOn = false;
			resetHoldoff.trigger(1e-3f);
		}
		bool holdoff = resetHoldoff.process(args.sampleTime);

		if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f) && !holdoff) {
			int n = numSteps();
			step = (step + 1 >= n) ? 0 : step + 1;
			for (int i = 0; i < numVoices; i++) {
				Voice& v = voices[i];
				int p = v.pattern.pitch[step];
				if (p >= 0) {
					v.cv = (snapToScale(p) - 12) / 12.f;
					// The first step of the loop always starts a new note.
					if ((step == 0 || !v.pattern.tie[step]) && v.noteOn)
						v.retrigger.trigger(1e-3f);
					v.noteOn = true;
				}
				else {
					v.noteOn = false;
				}
			}
		}

		for (int i = 0; i < numVoices; i++) {
			Voice& v = voices[i];
			bool retriggering = v.retrigger.process(args.sampleTime);
			outputs[VOCT_OUTPUT].setVoltage(v.cv + params[OCTAVE_PARAMS + i].getValue(), i);
			outputs[GATE_OUTPUT].setVoltage(v.noteOn && !retriggering ? 10.f : 0.f, i);
		}
		outputs[VOCT_OUTPUT].setChannels(numVoices);
		outputs[GATE_OUTPUT].setChannels(numVoices);

		if (lightDivider.process()) {
			for (int v = 0; v < MAX_VOICES; v++) {
				float b = (v == activeVoice) ? 1.f : (v < numVoices ? 0.25f : 0.05f);
				lights[VOICE_LIGHTS + v].setBrightness(b);
			}
		}
	}

	static json_t* patternToJson(const Pattern& pattern) {
		// Save up to the last used step, including notes outside the current loop.
		int last = MAX_STEPS - 1;
		while (last >= 0 && pattern.pitch[last] < 0) last--;
		json_t* j = json_object();
		json_t* pitches = json_array();
		json_t* ties = json_array();
		for (int i = 0; i <= last; i++) {
			json_array_append_new(pitches, json_integer(pattern.pitch[i]));
			json_array_append_new(ties, json_boolean(pattern.tie[i]));
		}
		json_object_set_new(j, "pitch", pitches);
		json_object_set_new(j, "tie", ties);
		return j;
	}

	static void patternFromJson(Pattern& pattern, json_t* j) {
		pattern.clear();
		json_t* pitches = json_object_get(j, "pitch");
		json_t* ties = json_object_get(j, "tie");
		for (int i = 0; i < MAX_STEPS; i++) {
			if (json_t* p = json_array_get(pitches, i)) {
				int v = json_integer_value(p);
				pattern.pitch[i] = (v >= 0 && v < NUM_PITCHES) ? v : -1;
			}
			if (json_t* t = json_array_get(ties, i))
				pattern.tie[i] = i > 0 && json_is_true(t) && pattern.pitch[i] >= 0 && pattern.pitch[i] == pattern.pitch[i - 1];
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "bars", json_integer(bars));
		json_object_set_new(rootJ, "beatsPerBar", json_integer(beatsPerBar));
		json_object_set_new(rootJ, "scale", json_string(SCALES[scale].id));
		json_object_set_new(rootJ, "root", json_integer(root));
		json_object_set_new(rootJ, "numVoices", json_integer(numVoices));
		json_object_set_new(rootJ, "activeVoice", json_integer(activeVoice));
		// All four voices are saved, so lowering the voice count doesn't lose notes.
		json_t* voicesJ = json_array();
		for (const Voice& v : voices)
			json_array_append_new(voicesJ, patternToJson(v.pattern));
		json_object_set_new(rootJ, "voices", voicesJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "bars"))
			bars = clamp((int) json_integer_value(j), 1, MAX_BARS);
		if (json_t* j = json_object_get(rootJ, "beatsPerBar"))
			beatsPerBar = clamp((int) json_integer_value(j), 1, MAX_BEATS_PER_BAR);
		scale = 0;
		if (json_t* j = json_object_get(rootJ, "scale")) {
			for (int i = 0; i < (int) SCALES.size(); i++)
				if (json_is_string(j) && std::string(json_string_value(j)) == SCALES[i].id)
					scale = i;
		}
		if (json_t* j = json_object_get(rootJ, "root"))
			root = clamp((int) json_integer_value(j), 0, 11);

		json_t* voicesJ = json_object_get(rootJ, "voices");
		for (int v = 0; v < MAX_VOICES; v++) {
			json_t* j = voicesJ ? json_array_get(voicesJ, v) : nullptr;
			// Patches from before polyphony stored one pattern at the top level.
			if (!voicesJ && v == 0)
				j = rootJ;
			if (j)
				patternFromJson(voices[v].pattern, j);
			else
				voices[v].pattern.clear();
		}

		numVoices = 1;
		if (json_t* j = json_object_get(rootJ, "numVoices"))
			numVoices = clamp((int) json_integer_value(j), 1, MAX_VOICES);
		activeVoice = 0;
		if (json_t* j = json_object_get(rootJ, "activeVoice"))
			activeVoice = clamp((int) json_integer_value(j), 0, numVoices - 1);
	}
};

struct PianoRollDisplay : Widget {
	PianoRoll* module = nullptr;
	float keysWidth = 0.f;
	float overviewHeight = 0.f;
	float overviewGap = 0.f;

	int viewBar = 0;  // first bar shown in the grid

	enum DragMode { DRAG_NONE, DRAG_NOTE, DRAG_OVERVIEW };
	DragMode dragMode = DRAG_NONE;
	// The pattern as it was at mouse-down, so each drag move re-applies the
	// note from scratch (supports shrinking as well as growing).
	Pattern dragSnapshot;
	int dragVoice = 0, dragStep = 0, dragPitch = 0;
	Vec dragPos;

	// Browser-preview defaults when there is no module.
	int bars() { return module ? module->bars : 4; }
	int stepsPerBar() { return (module ? module->beatsPerBar : 4) * STEPS_PER_BEAT; }
	int numSteps() { return bars() * stepsPerBar(); }

	int barsPerView() { return clamp(MAX_VISIBLE_STEPS / stepsPerBar(), 1, bars()); }
	int viewSteps() { return barsPerView() * stepsPerBar(); }
	int firstViewStep() { return viewBar * stepsPerBar(); }

	float gridTop() { return overviewHeight + overviewGap; }
	float gridWidth() { return box.size.x - keysWidth; }
	float cellW() { return gridWidth() / viewSteps(); }
	float cellH() { return (box.size.y - gridTop()) / NUM_PITCHES; }
	float overviewBarW() { return gridWidth() / bars(); }

	bool inScale(int p) { return module ? module->inScale(p) : true; }
	bool isRoot(int p) { return module && p % 12 == module->root; }

	static bool isBlackKey(int p) {
		int n = p % 12;
		return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
	}

	void clampView() { viewBar = clamp(viewBar, 0, bars() - barsPerView()); }

	int stepAt(float x) {
		int local = clamp((int) ((x - keysWidth) / cellW()), 0, viewSteps() - 1);
		return firstViewStep() + local;
	}
	int pitchAt(float y) {
		return clamp(NUM_PITCHES - 1 - (int) ((y - gridTop()) / cellH()), 0, NUM_PITCHES - 1);
	}

	// Centre the view window on the bar under x in the overview strip.
	void scrollOverviewTo(float x) {
		int bar = (int) std::floor((x - keysWidth) / overviewBarW());
		viewBar = bar - barsPerView() / 2;
		clampView();
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS)
			return;
		if (e.pos.x < keysWidth)
			return;
		e.consume(this);
		clampView();
		dragPos = e.pos;
		dragMode = DRAG_NONE;

		if (e.pos.y < gridTop()) {
			dragMode = DRAG_OVERVIEW;
			scrollOverviewTo(e.pos.x);
			return;
		}

		// Clicks only ever edit the active voice; other voices' notes are
		// just shown underneath.
		int s = stepAt(e.pos.x);
		int p = pitchAt(e.pos.y);
		Pattern& pat = module->activePattern();
		if (pat.pitch[s] == p) {
			pat.deleteNote(s);
			return;
		}
		if (!inScale(p))
			return;
		dragSnapshot = pat;
		dragVoice = module->activeVoice;
		dragStep = s;
		dragPitch = p;
		dragMode = DRAG_NOTE;
		pat.setNote(s, s, p);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!module || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
		if (dragMode == DRAG_OVERVIEW) {
			scrollOverviewTo(dragPos.x);
		}
		else if (dragMode == DRAG_NOTE) {
			int end = std::max(dragStep, stepAt(dragPos.x));
			Pattern pat = dragSnapshot;
			pat.setNote(dragStep, end, dragPitch);
			module->voices[dragVoice].pattern = pat;
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		dragMode = DRAG_NONE;
	}

	void line(NVGcontext* vg, float x0, float y0, float x1, float y1, NVGcolor c, float w) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, x0, y0);
		nvgLineTo(vg, x1, y1);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, w);
		nvgStroke(vg);
	}

	void rect(NVGcontext* vg, float x, float y, float w, float h, NVGcolor c) {
		nvgBeginPath(vg);
		nvgRect(vg, x, y, w, h);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}

	void draw(const DrawArgs& args) override {
		clampView();
		NVGcontext* vg = args.vg;
		float cw = cellW(), ch = cellH(), top = gridTop();
		int spb = stepsPerBar();

		rect(vg, 0, 0, box.size.x, box.size.y, nvgRGB(0x1b, 0x1c, 0x20));

		// Overview background with bar dividers
		rect(vg, keysWidth, 0, gridWidth(), overviewHeight, nvgRGB(0x24, 0x26, 0x2c));
		for (int b = 1; b < bars(); b++) {
			float x = keysWidth + b * overviewBarW();
			line(vg, x, 0, x, overviewHeight, nvgRGB(0x3a, 0x3c, 0x44), 0.6f);
		}

		for (int p = 0; p < NUM_PITCHES; p++) {
			float y = top + (NUM_PITCHES - 1 - p) * ch;
			bool black = isBlackKey(p);
			bool allowed = inScale(p);
			if (!allowed) {
				// Out of scale: dark, with diagonal hatching
				rect(vg, keysWidth, y, gridWidth(), ch, nvgRGB(0x0e, 0x0f, 0x11));
				nvgSave(vg);
				nvgScissor(vg, keysWidth, y, gridWidth(), ch);
				nvgBeginPath(vg);
				for (float hx = keysWidth - ch; hx < box.size.x; hx += ch * 0.8f) {
					nvgMoveTo(vg, hx, y + ch);
					nvgLineTo(vg, hx + ch, y);
				}
				nvgStrokeColor(vg, nvgRGB(0x1c, 0x1d, 0x21));
				nvgStrokeWidth(vg, 0.5f);
				nvgStroke(vg);
				nvgRestore(vg);
			}
			else if (black) {
				rect(vg, keysWidth, y, gridWidth(), ch, nvgRGB(0x14, 0x15, 0x18));
			}
			nvgBeginPath(vg);
			nvgRect(vg, 0, y + 0.25f, keysWidth - 0.5f, ch - 0.5f);
			NVGcolor key = black ? nvgRGB(0x2b, 0x2d, 0x33) : nvgRGB(0xe8, 0xe4, 0xda);
			if (!allowed)
				key = black ? nvgRGB(0x1e, 0x1f, 0x23) : nvgRGB(0x80, 0x7d, 0x76);
			nvgFillColor(vg, key);
			nvgFill(vg);
			if (isRoot(p))
				rect(vg, keysWidth - 2.5f, y + 0.25f, 2.0f, ch - 0.5f, nvgRGB(0x8a, 0x8c, 0x94));
			if (p % 12 == 0)
				line(vg, keysWidth, y + ch, box.size.x, y + ch, nvgRGB(0x3a, 0x3c, 0x44), 0.8f);
		}

		// Vertical lines: steps, beats, bars
		int first = firstViewStep();
		for (int i = 0; i <= viewSteps(); i++) {
			int s = first + i;
			float x = keysWidth + i * cw;
			if (s % spb == 0)
				line(vg, x, top, x, box.size.y, nvgRGB(0x70, 0x72, 0x7a), 1.0f);
			else if (s % STEPS_PER_BEAT == 0)
				line(vg, x, top, x, box.size.y, nvgRGB(0x40, 0x42, 0x4a), 0.7f);
			else
				line(vg, x, top, x, box.size.y, nvgRGB(0x26, 0x28, 0x2e), 0.5f);
		}
		Widget::draw(args);
	}

	// Draw one voice's notes in the visible part of the grid. Inactive voices
	// are drawn translucent so the voice being edited stands out.
	void drawVoiceNotes(NVGcontext* vg, const Pattern& pat, NVGcolor color, bool active, int playStep) {
		float cw = cellW(), ch = cellH(), top = gridTop();
		int n = numSteps();
		int first = firstViewStep(), last = first + viewSteps() - 1;
		float alpha = active ? 1.f : 0.45f;

		// Start one note earlier than the view so notes that begin off-screen
		// to the left are still drawn.
		int s = pat.noteStart(first);
		while (s <= last && s < n) {
			int p = pat.pitch[s];
			if (p < 0) {
				s++;
				continue;
			}
			int end = std::min(pat.noteEnd(s, n), n - 1);
			int a = std::max(s, first), b = std::min(end, last);
			float x = keysWidth + (a - first) * cw + 0.5f;
			float y = top + (NUM_PITCHES - 1 - p) * ch + 0.5f;
			float w = (b - a + 1) * cw - 1.f;
			bool playing = playStep >= s && playStep <= end;
			NVGcolor c = playing ? color::plus(color, nvgRGB(0x40, 0x40, 0x40)) : color;

			nvgBeginPath(vg);
			nvgRoundedRect(vg, x, y, w, ch - 1.f, 1.f);
			if (inScale(p)) {
				nvgFillColor(vg, color::alpha(c, alpha));
				nvgFill(vg);
			}
			else {
				// Out of scale: plays snapped to the nearest scale note.
				nvgFillColor(vg, color::alpha(c, alpha * (playing ? 0.55f : 0.25f)));
				nvgFill(vg);
				nvgStrokeColor(vg, color::alpha(c, alpha));
				nvgStrokeWidth(vg, 0.6f);
				nvgStroke(vg);
			}
			if (active) {
				nvgStrokeColor(vg, nvgRGBA(0x00, 0x00, 0x00, 0x60));
				nvgStrokeWidth(vg, 0.5f);
				nvgStroke(vg);
			}
			s = end + 1;
		}
	}

	// Notes and playhead go on the light layer so they stay bright when the
	// room lights are dimmed.
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			clampView();
			NVGcontext* vg = args.vg;
			float cw = cellW(), top = gridTop();
			int n = numSteps();
			int first = firstViewStep(), last = first + viewSteps() - 1;
			int playStep = module ? module->step : -1;
			if (playStep >= n)
				playStep = -1;

			// Collect the patterns to draw. In the module browser there's no
			// module, so show a two-voice demo.
			Pattern demo[2];
			const Pattern* pats[MAX_VOICES];
			int count, active;
			if (module) {
				count = module->numVoices;
				active = module->activeVoice;
				for (int v = 0; v < count; v++)
					pats[v] = &module->voices[v].pattern;
			}
			else {
				demo[0].clear();
				demo[1].clear();
				const int arp[] = {12, 16, 19, 23, 19, 16};
				for (int i = 0; i < 16; i++)
					demo[0].setNote(i * 4, i * 4 + 2, arp[i % 6]);
				const int bass[] = {0, 5, 7, 0};
				for (int i = 0; i < 4; i++)
					demo[1].setNote(i * 16, i * 16 + 11, bass[i]);
				pats[0] = &demo[0];
				pats[1] = &demo[1];
				count = 2;
				active = 0;
			}

			// Overview: miniature notes, visible window, playhead
			float sx = gridWidth() / n;
			float sy = overviewHeight / NUM_PITCHES;
			for (int v = 0; v < count; v++) {
				NVGcolor c = color::alpha(VOICE_COLORS[v], v == active ? 1.f : 0.6f);
				for (int s = 0; s < n; s++) {
					int p = pats[v]->pitch[s];
					if (p < 0)
						continue;
					rect(vg, keysWidth + s * sx, (NUM_PITCHES - 1 - p) * sy, std::max(sx, 0.5f), std::max(sy, 0.5f), c);
				}
			}
			nvgBeginPath(vg);
			nvgRect(vg, keysWidth + first * sx + 0.4f, 0.4f, viewSteps() * sx - 0.8f, overviewHeight - 0.8f);
			nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x90));
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);
			if (playStep >= 0)
				rect(vg, keysWidth + playStep * sx, 0, std::max(sx, 1.f), overviewHeight, nvgRGBA(0xff, 0xff, 0xff, 0x60));

			// Grid playhead
			if (playStep >= first && playStep <= last)
				rect(vg, keysWidth + (playStep - first) * cw, top, cw, box.size.y - top, nvgRGBA(0xff, 0xff, 0xff, 0x22));

			// Inactive voices first, then the active voice on top.
			for (int v = 0; v < count; v++)
				if (v != active)
					drawVoiceNotes(vg, *pats[v], VOICE_COLORS[v], false, playStep);
			drawVoiceNotes(vg, *pats[active], VOICE_COLORS[active], true, playStep);
		}
		Widget::drawLayer(args, layer);
	}
};

// A light tinted with a voice's color, for the voice-select buttons.
template <int V>
struct VoiceLight : GrayModuleLightWidget {
	VoiceLight() { addBaseColor(VOICE_COLORS[V]); }
};

struct PianoRollWidget : ModuleWidget {
	PianoRollWidget(PianoRoll* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PianoRoll.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Display area matches the bezel in panels/PianoRoll.svg.
		PianoRollDisplay* display = createWidget<PianoRollDisplay>(mm2px(Vec(4.0, 16.0)));
		display->box.size = mm2px(Vec(144.4, 72.0));
		display->keysWidth = mm2px(7.0);
		display->overviewHeight = mm2px(4.5);
		display->overviewGap = mm2px(0.6);
		display->module = module;
		addChild(display);

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.0, 106.0)), module, PianoRoll::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(36.0, 106.0)), module, PianoRoll::RESET_INPUT));

		// One column per voice: select button on top, octave trimpot below.
		// Column x positions match panels/PianoRoll.svg.
		const float voiceX[MAX_VOICES] = {52.0, 62.0, 72.0, 82.0};
		addParam(createLightParamCentered<VCVLightBezel<VoiceLight<0>>>(mm2px(Vec(voiceX[0], 99.5)), module, PianoRoll::VOICE_PARAMS + 0, PianoRoll::VOICE_LIGHTS + 0));
		addParam(createLightParamCentered<VCVLightBezel<VoiceLight<1>>>(mm2px(Vec(voiceX[1], 99.5)), module, PianoRoll::VOICE_PARAMS + 1, PianoRoll::VOICE_LIGHTS + 1));
		addParam(createLightParamCentered<VCVLightBezel<VoiceLight<2>>>(mm2px(Vec(voiceX[2], 99.5)), module, PianoRoll::VOICE_PARAMS + 2, PianoRoll::VOICE_LIGHTS + 2));
		addParam(createLightParamCentered<VCVLightBezel<VoiceLight<3>>>(mm2px(Vec(voiceX[3], 99.5)), module, PianoRoll::VOICE_PARAMS + 3, PianoRoll::VOICE_LIGHTS + 3));
		for (int v = 0; v < MAX_VOICES; v++)
			addParam(createParamCentered<Trimpot>(mm2px(Vec(voiceX[v], 110.0)), module, PianoRoll::OCTAVE_PARAMS + v));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(116.4, 106.0)), module, PianoRoll::VOCT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(132.4, 106.0)), module, PianoRoll::GATE_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		PianoRoll* module = getModule<PianoRoll>();
		menu->addChild(new MenuSeparator);

		menu->addChild(createSubmenuItem("Voices", string::f("%d", module->numVoices), [=](Menu* menu) {
			for (int v = 1; v <= MAX_VOICES; v++) {
				menu->addChild(createCheckMenuItem(string::f("%d", v), "",
					[=]() { return module->numVoices == v; },
					[=]() { module->setNumVoices(v); }));
			}
		}));
		menu->addChild(createSubmenuItem("Bars", string::f("%d", module->bars), [=](Menu* menu) {
			for (int b = 1; b <= MAX_BARS; b++) {
				menu->addChild(createCheckMenuItem(string::f("%d", b), "",
					[=]() { return module->bars == b; },
					[=]() { module->bars = b; }));
			}
		}));
		menu->addChild(createSubmenuItem("Beats per bar", string::f("%d", module->beatsPerBar), [=](Menu* menu) {
			for (int b = 1; b <= MAX_BEATS_PER_BAR; b++) {
				menu->addChild(createCheckMenuItem(string::f("%d", b), "",
					[=]() { return module->beatsPerBar == b; },
					[=]() { module->beatsPerBar = b; }));
			}
		}));

		menu->addChild(createSubmenuItem("Scale", SCALES[module->scale].name, [=](Menu* menu) {
			for (int i = 0; i < (int) SCALES.size(); i++) {
				menu->addChild(createCheckMenuItem(SCALES[i].name, "",
					[=]() { return module->scale == i; },
					[=]() { module->scale = i; }));
			}
		}));
		menu->addChild(createSubmenuItem("Root", NOTE_NAMES[module->root], [=](Menu* menu) {
			for (int i = 0; i < 12; i++) {
				menu->addChild(createCheckMenuItem(NOTE_NAMES[i], "",
					[=]() { return module->root == i; },
					[=]() { module->root = i; }));
			}
		}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem(string::f("Clear voice %d", module->activeVoice + 1), "",
			[=]() { module->activePattern().clear(); }));
		menu->addChild(createMenuItem("Clear all voices", "", [=]() {
			for (PianoRoll::Voice& v : module->voices)
				v.pattern.clear();
		}));
	}
};

Model* modelPianoRoll = createModel<PianoRoll, PianoRollWidget>("PianoRoll");
