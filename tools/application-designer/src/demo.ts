import type { ApplicationDocument, ModuleDescriptor } from './model'

const audio = 'CommRaT::Messages::AudioBlock'
const notes = 'CommRaT::Messages::NoteEventBlock'
const parameters = 'CommRaT::Messages::ParameterEventBlock'

export const demoDescriptors: ModuleDescriptor[] = [
  {
    module_class: 'MusicRaTNoteKeyboard', binary: '', outputs: [notes], inputs: [],
    params_defaults: { channel: 0 },
    descriptor_metadata: { musicrat_ports: { schema_version: 1, ports: [
      { id: 'note_events', display_name: 'Notes', direction: 'output', port_index: 0, domain: 'note', required: true, max_connections: 1 },
    ] } },
  },
  {
    module_class: 'MusicRaTInstrument', binary: '', outputs: [audio], inputs: [notes], synced_inputs: [parameters],
    params_defaults: { gain: 0.8 },
    descriptor_metadata: {
      musicrat_ports: { schema_version: 1, ports: [
        { id: 'audio_out', display_name: 'Audio Out', direction: 'output', port_index: 0, domain: 'audio', required: true, max_connections: 1 },
        { id: 'note_events', display_name: 'Notes', direction: 'input', port_index: 0, domain: 'note', required: true, max_connections: 1 },
        { id: 'parameter_events', display_name: 'Parameters', direction: 'synced_input', port_index: 0, domain: 'parameter', required: false, max_connections: 1 },
      ] },
      musicrat_parameters: { schema_version: 1, parameters: [
        { id: 1, name: 'gain', display_name: 'Gain', group: 'Level', kind: 'continuous', unit: 'x', minimum: 0, maximum: 1, step: 0.01, display_scale: 'linear', automatable: true, read_only: false, choices: [] },
      ] },
    },
  },
  {
    module_class: 'MusicRaTStereoPanner', binary: '', outputs: [audio], inputs: [audio], synced_inputs: [parameters],
    params_defaults: { pan: 0, pan_law: 1, smoothing_samples: 64 },
    descriptor_metadata: {
      musicrat_ports: { schema_version: 1, ports: [
        { id: 'audio_out', display_name: 'Stereo Out', direction: 'output', port_index: 0, domain: 'audio', required: true, max_connections: 1 },
        { id: 'audio_in', display_name: 'Mono In', direction: 'input', port_index: 0, domain: 'audio', required: true, max_connections: 1 },
        { id: 'parameter_events', display_name: 'Parameters', direction: 'synced_input', port_index: 0, domain: 'parameter', required: false, max_connections: 1 },
      ] },
    },
  },
  {
    module_class: 'MusicRaTWavSink', binary: '', outputs: [], inputs: [audio],
    params_defaults: { path: '/tmp/musicrat-designer.wav', sample_rate_hz: 48000, channel_count: 2 },
    descriptor_metadata: { musicrat_ports: { schema_version: 1, ports: [
      { id: 'audio_in', display_name: 'Audio In', direction: 'input', port_index: 0, domain: 'audio', required: true, max_connections: 1 },
    ] } },
  },
]

export const demoApplication: ApplicationDocument = {
  app_name: 'DesignerNoteRig',
  modules: [
    { name: 'Keyboard_1', module_class: 'MusicRaTNoteKeyboard', outputs: [{ system_id: 10, instance_id: 1 }], params: { channel: 0 } },
    { name: 'Instrument_1', module_class: 'MusicRaTInstrument', outputs: [{ system_id: 10, instance_id: 2 }], inputs: [{ source_system_id: 10, source_instance_id: 1 }], params: { gain: 0.8 } },
    { name: 'StereoPanner_1', module_class: 'MusicRaTStereoPanner', outputs: [{ system_id: 10, instance_id: 3 }], inputs: [{ source_system_id: 10, source_instance_id: 2 }], params: { pan: 0, pan_law: 1, smoothing_samples: 64 } },
    { name: 'WavSink_1', module_class: 'MusicRaTWavSink', outputs: [], inputs: [{ source_system_id: 10, source_instance_id: 3 }], params: { path: '/tmp/musicrat-designer.wav', sample_rate_hz: 48000, channel_count: 2 } },
  ],
}