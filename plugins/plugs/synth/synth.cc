#include "synth.hh"

#include <cstring>

#include "../../audio-buffer.hh"
#include "../../modules/adsr-module.hh"
#include "../../modules/digi-osc-module.hh"
#include "../../modules/svf-module.hh"
#include "../../modules/voice-expander-module.hh"
#include "../modules/module.hh"
#include "synth.hh"

namespace clap {
   namespace {
      enum ModuleIdentifiers {
         SynthId = 0,
         VoiceExpanderId = 1,
         FltAdsrId = 2,
         FltId = 3,
         Osc1Id = 4,
         Osc2Id = 5,
         AmpAdsrId = 6,
      };

      class SynthVoiceModule final : public Module {
         using super = Module;

      public:
         SynthVoiceModule(Synth &synth)
            : Module(synth, "", 0), _ampAdsr(synth, "amp", AmpAdsrId),
              _filterAdsr(synth, "filter env", FltAdsrId), _filter(synth, "filter", FltId),
              _digiOsc1(synth, "osc1", Osc1Id), _digiOsc2(synth, "osc2", Osc2Id) {
            performRouting();
         }

         SynthVoiceModule(const SynthVoiceModule &m)
            : Module(m), _ampAdsr(m._ampAdsr), _filterAdsr(m._filterAdsr), _filter(m._filter),
              _digiOsc1(m._digiOsc1), _digiOsc2(m._digiOsc2) {
            performRouting();
         }

         std::unique_ptr<Module> cloneVoice() const override {
            return std::make_unique<SynthVoiceModule>(*this);
         }

         void performRouting() {
            _digiOsc1.setPmInput(&_digiOsc2.outputBuffer());
            _filter.setInput(&_oscMixBuffer);
         }

         bool doActivate(double sampleRate, uint32_t maxFrameCount) override {
            bool succeed = true;

            succeed &= _ampAdsr.activate(sampleRate, maxFrameCount);
            succeed &= _filterAdsr.activate(sampleRate, maxFrameCount);
            succeed &= _filter.activate(sampleRate, maxFrameCount);
            succeed &= _digiOsc1.activate(sampleRate, maxFrameCount);
            succeed &= _digiOsc2.activate(sampleRate, maxFrameCount);

            if (succeed)
               return true;

            deactivate();

            return false;
         }

         void doDeactivate() override {
            _ampAdsr.deactivate();
            _filterAdsr.deactivate();
            _filter.deactivate();
            _digiOsc1.deactivate();
            _digiOsc2.deactivate();
         }

         clap_process_status process(const Context &c, uint32_t numFrames) noexcept override {
            _digiOsc2.process(c, numFrames);
            _digiOsc1.process(c, numFrames);

            _oscMixBuffer.sum(_digiOsc1.outputBuffer(), _digiOsc2.outputBuffer(), numFrames);

            _filter.process(c, numFrames);

            auto status = _ampAdsr.process(c, numFrames);
            _voiceModule->outputBuffer().product(_filter.outputBuffer(), _ampAdsr.outputBuffer(), numFrames);

            return status;
         }

         bool wantsNoteEvents() const noexcept override { return true; }

         void onNoteOn(const clap_event_note &note) noexcept override {
            _ampAdsr.onNoteOn(note);
            _filterAdsr.onNoteOn(note);
         }

         void onNoteOff(const clap_event_note &note) noexcept override {
            _ampAdsr.onNoteOff(note);
            _filterAdsr.onNoteOff(note);
         }

         void onNoteChoke(const clap_event_note &note) noexcept override {
            _ampAdsr.onNoteChoke(note);
            _filterAdsr.onNoteChoke(note);
         }

         void setVoiceModule(const VoiceModule *voiceModule) noexcept override {
            super::setVoiceModule(voiceModule);
            _ampAdsr.setVoiceModule(voiceModule);
            _filterAdsr.setVoiceModule(voiceModule);
            _filter.setVoiceModule(voiceModule);
            _digiOsc1.setVoiceModule(voiceModule);
            _digiOsc2.setVoiceModule(voiceModule);
         }

      protected:
         AdsrModule _ampAdsr;
         AdsrModule _filterAdsr;
         SvfModule _filter;
         DigiOscModule _digiOsc1;
         DigiOscModule _digiOsc2;

         AudioBuffer<double> _oscMixBuffer{1, BLOCK_SIZE};
      };

      class SynthModule final : public Module {
      public:
         SynthModule(Synth &synth)
            : Module(synth, "", SynthId),
              _expanderModule(synth, 0, std::make_unique<SynthVoiceModule>(synth), 1) {}

         SynthModule(const SynthModule &m) = delete;

         bool doActivate(double sampleRate, uint32_t maxFrameCount) override {
            return _expanderModule.activate(sampleRate, maxFrameCount);
         }

         void doDeactivate() override { _expanderModule.deactivate(); }

         clap_process_status process(const Context &c, uint32_t numFrames) noexcept override {
            auto status = _expanderModule.process(c, numFrames);
            c.audioOutputs[0]->copy(_expanderModule.outputBuffer(), numFrames);
            return status;
         }

         bool wantsNoteEvents() const noexcept override { return true; }

         void onNoteOn(const clap_event_note &note) noexcept override {
            _expanderModule.onNoteOn(note);
         }

         void onNoteOff(const clap_event_note &note) noexcept override {
            _expanderModule.onNoteOff(note);
         }

         void onNoteChoke(const clap_event_note &note) noexcept override {
            _expanderModule.onNoteChoke(note);
         }

         void onNoteExpression(const clap_event_note_expression &noteExp) noexcept override {
            _expanderModule.onNoteExpression(noteExp);
         }

      private:
         VoiceExpanderModule _expanderModule;
      };
   } // namespace

   const clap_plugin_descriptor *Synth::descriptor() {
      static const char *features[] = {"instrument", nullptr};

      static const clap_plugin_descriptor desc = {

         CLAP_VERSION,
         "com.github.free-audio.clap.synth",
         "CLAP Synth",
         "clap",
         "https://github.com/free-audio/clap-plugins",
         nullptr,
         nullptr,
         "0.1",
         "Example Synthesizer plugin",
         features};
      return &desc;
   }

   Synth::Synth(const std::string &pluginPath, const clap_host *host)
      : CorePlugin(PathProvider::create(pluginPath, "synth"), descriptor(), host) {
      _rootModule = std::make_unique<SynthModule>(*this);
   }

   bool Synth::init() noexcept {
      if (!super::init())
         return false;

      defineAudioPorts();
      return true;
   }

   void Synth::defineAudioPorts() noexcept {
      assert(!isActive());

      clap_audio_port_info info;
      info.id = 0;
      strncpy(info.name, "main", sizeof(info.name));
      info.flags = CLAP_AUDIO_PORT_IS_MAIN;
      info.in_place_pair = CLAP_INVALID_ID;
      info.channel_count = 2;
      info.port_type = CLAP_PORT_STEREO;

      _audioInputs.clear();
      _audioOutputs.clear();
      _audioOutputs.push_back(info);
   }
} // namespace clap
