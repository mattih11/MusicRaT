import { describe, expect, it } from 'vitest'
import { compileControlPlan, compileLaunchApplication } from './control-compiler'
import { demoApplication, demoDescriptors } from './demo'
import { importApplication, type MusicRaTControlProject } from './model'
import { observableTargets } from './graph-targets'

function workspaceWithControl(control: MusicRaTControlProject) {
  const workspace = importApplication(
    structuredClone(demoApplication),
    structuredClone(demoDescriptors),
  )
  workspace.modules.push({
    id: 'FaderAdapter_1',
    descriptor: {
      module_class: 'MusicRaTFaderAdapter',
      binary: '',
      outputs: ['CommRaT::Messages::ControlEventBlock'],
      descriptor_metadata: { musicrat_ports: { schema_version: 1, ports: [{
        id: 'control_events',
        display_name: 'Control Events',
        direction: 'output',
        port_index: 0,
        domain: 'control',
        required: true,
        max_connections: 1,
      }] } },
    },
    source: {
      name: 'FaderAdapter_1',
      module_class: 'MusicRaTFaderAdapter',
      outputs: [{ system_id: 10, instance_id: 4 }],
    },
    position: { x: 0, y: 0 },
  })
  workspace.source.musicrat_control = control
  return workspace
}

const control: MusicRaTControlProject = {
  schema_version: 1,
  devices: [{
    id: 'faders',
    display_name: 'Faders',
    kind: 'hardware',
    adapter_module_id: 'FaderAdapter_1',
    endpoints: [{
      id: 'gain',
      display_name: 'Gain',
      direction: 'input',
      domain: 'unipolar',
    }],
  }],
  bindings: [{
    id: 'instrument-gain',
    source: { owner_id: 'faders', endpoint_id: 'gain' },
    target: { module_id: 'Instrument_1', parameter_id: 1 },
    mode: 'absolute',
    pickup: 'match',
    pickup_tolerance: 0.02,
    transform: { scale: 0.8, offset: 0.1, curve: 'exponential' },
  }],
  surfaces: [],
}

describe('control launch compiler', () => {
  it('exports playback time, position and play state through the existing LVGL observation path', () => {
    const deckControl = structuredClone(control)
    deckControl.bindings = []
    deckControl.deck = {
      schema_version: 1,
      panel: { width_mm: 160, height_mm: 90, thickness_mm: 3 },
      elements: [{
        id: 'screen', component_id: 'touch-display-5in', display_name: 'Deck',
        position: { x_mm: 0, y_mm: 0 },
      }],
    }
    deckControl.surfaces = [{
      id: 'display-screen', display_name: 'Deck', target: 'lvgl', endpoints: [],
      widgets: [{
        id: 'elapsed', kind: 'text', display_name: 'Elapsed',
        layout: { x: 0, y: 0, width: 40, height: 20 }, route: 'feedback',
      }, {
        id: 'position', kind: 'meter', display_name: 'Position',
        layout: { x: 0, y: 20, width: 40, height: 20 }, route: 'feedback',
      }, {
        id: 'playing', kind: 'toggle', display_name: 'Playing',
        layout: { x: 0, y: 40, width: 40, height: 20 }, route: 'feedback',
      }],
    }]
    deckControl.observation_bindings = [
      { id: 'elapsed', source: { module_id: 'Deck', observable_id: 'elapsed' },
        target: { surface_id: 'display-screen', widget_id: 'elapsed', property: 'text' } },
      { id: 'position', source: { module_id: 'Deck', observable_id: 'position' },
        target: { surface_id: 'display-screen', widget_id: 'position', property: 'value' } },
      { id: 'playing', source: { module_id: 'Deck', observable_id: 'playing' },
        target: { surface_id: 'display-screen', widget_id: 'playing', property: 'active' } },
    ]
    const workspace = workspaceWithControl(deckControl)
    workspace.modules.push({
      id: 'Deck',
      descriptor: {
        module_class: 'MusicRaTAudioFilePlayer', binary: '',
        outputs: ['CommRaT::Messages::AudioBlock', 'CommRaT::Messages::PlaybackStatusBlock'],
        descriptor_metadata: {
          musicrat_ports: { schema_version: 1, ports: [{
            id: 'playback_status', display_name: 'Status', direction: 'output',
            port_index: 1, domain: 'telemetry', required: true, max_connections: 1,
          }] },
          musicrat_observables: { schema_version: 1, observables: [
            { id: 'elapsed', display_name: 'Elapsed', group: 'Timeline',
              output_port_id: 'playback_status', selector: 'elapsed_time',
              kind: 'text', unit: '', minimum: 0, maximum: 1, channel_selectable: false },
            { id: 'position', display_name: 'Position', group: 'Timeline',
              output_port_id: 'playback_status', selector: 'position',
              kind: 'continuous', unit: '', minimum: 0, maximum: 1, channel_selectable: false },
            { id: 'playing', display_name: 'Playing', group: 'State',
              output_port_id: 'playback_status', selector: 'flags.playing',
              kind: 'boolean', unit: '', minimum: 0, maximum: 1, channel_selectable: false },
          ] },
        },
      },
      source: {
        name: 'Deck', module_class: 'MusicRaTAudioFilePlayer',
        outputs: [{ system_id: 10, instance_id: 20 }, { system_id: 30, instance_id: 20 }],
      },
      position: { x: 0, y: 0 },
    })
    expect(observableTargets(workspace.modules).filter((item) => item.module.id === 'Deck')).toHaveLength(3)
    const plan = compileControlPlan(workspace)
    expect(plan.observation_adapters).toHaveLength(1)
    expect(plan.observation_adapters[0]).toMatchObject({
      module_class: 'MusicRaTPlaybackStatusUiAdapter',
      source_module_id: 'Deck', source_port_id: 'playback_status',
      params: { bindings: [
        { selector: 10, property: 2 },
        { selector: 5, property: 1 },
        { selector: 9, property: 0, minimum: 0, maximum: 1 },
      ] },
    })
    const launch = compileLaunchApplication(workspace)
    const adapter = launch.modules.find((item) => item.module_class === 'MusicRaTPlaybackStatusUiAdapter')!
    const player = launch.modules.find((item) => item.name === 'Deck')!
    const sink = launch.modules.find((item) => item.module_class === 'MusicRaTLvglWidgetSink')!
    expect(adapter.inputs).toEqual([{
      source_system_id: player.outputs![1].system_id,
      source_instance_id: player.outputs![1].instance_id,
    }])
    expect(sink.synced_inputs).toEqual([{
      source_system_id: adapter.outputs![0].system_id,
      source_instance_id: adapter.outputs![0].instance_id,
    }])
    expect(adapter.params).toEqual(plan.observation_adapters[0].params)
    expect(launch.modules.indexOf(adapter)).toBeGreaterThan(launch.modules.indexOf(player))

    deckControl.observation_bindings[0].target.property = 'active'
    expect(() => compileControlPlan(workspace)).toThrow('incompatible')
    deckControl.observation_bindings[0].target.property = 'text'
    const positionObservable = workspace.modules.find((item) => item.id === 'Deck')!
      .descriptor.descriptor_metadata!.musicrat_observables!.observables[1]
    positionObservable.minimum = 1
    expect(() => compileControlPlan(workspace)).toThrow('invalid playback range')
  })

  it('emits required empty continuous input arrays for source modules', () => {
    const workspace = importApplication(structuredClone(demoApplication), structuredClone(demoDescriptors))
    const launch = compileLaunchApplication(workspace)
    for (const module of launch.modules) expect(Array.isArray(module.inputs)).toBe(true)
  })

  it('routes an LVGL keyboard note output to a selected note input', () => {
    const keyboardControl = structuredClone(control)
    keyboardControl.deck = {
      schema_version: 1,
      panel: { width_mm: 160, height_mm: 90, thickness_mm: 3 },
      elements: [{
        id: 'keyboard-display',
        component_id: 'touch-display-5in',
        display_name: 'Keyboard Display',
        position: { x_mm: 0, y_mm: 0 },
      }],
    }
    keyboardControl.surfaces.push({
      id: 'display-keyboard-display',
      display_name: 'Keyboard',
      target: 'lvgl',
      endpoints: [],
      widgets: [{
        id: 'keyboard',
        kind: 'keyboard',
        display_name: 'Keyboard',
        layout: { x: 5, y: 5, width: 90, height: 85 },
        route: 'control',
        note_target_module_id: 'ThreeOscillator_1',
      }],
    })
    const workspace = workspaceWithControl(keyboardControl)
    const noteModule = {
      name: 'ThreeOscillator_1',
      module_class: 'MusicRaTThreeOscillator',
      outputs: [{ system_id: 10, instance_id: 7 }],
      inputs: [],
      synced_inputs: [],
      params: {},
    }
    workspace.source.modules.push(noteModule)
    workspace.modules.push({
      id: noteModule.name,
      descriptor: {
        module_class: noteModule.module_class,
        binary: '',
        outputs: ['CommRaT::Messages::AudioBlock'],
        synced_inputs: ['CommRaT::Messages::NoteEventBlock'],
        descriptor_metadata: { musicrat_ports: { schema_version: 1, ports: [{
          id: 'note_events',
          display_name: 'Note Events',
          direction: 'synced_input',
          port_index: 0,
          domain: 'note',
          required: true,
          max_connections: 1,
        }] } },
      },
      source: noteModule,
      position: { x: 0, y: 0 },
    })

    const application = compileLaunchApplication(workspace)
    expect(application.modules[0].musicrat_generated).toBe('lvgl_widget_sink')
    const sink = application.modules.find(
      (module) => module.musicrat_generated === 'lvgl_widget_sink')!
    const target = application.modules.find(
      (module) => module.name === 'ThreeOscillator_1')!
    expect(sink.outputs).toHaveLength(2)
    expect(target.synced_inputs).toEqual([{
      source_system_id: sink.outputs![1].system_id,
      source_instance_id: sink.outputs![1].instance_id,
    }])
    expect(sink.params).toMatchObject({
      note_source_endpoint_id: sink.params!.device_id,
    })
  })

  it('compiles stable strings into exact bounded mapper parameters', () => {
    const plan = compileControlPlan(workspaceWithControl(control))

    expect(plan.owner_ids).toEqual([{ project_id: 'faders', runtime_id: 1 }])
    expect(plan.endpoint_ids).toEqual([{
      owner_id: 'faders', project_id: 'gain', runtime_id: 1,
    }])
    expect(plan.binding_ids).toEqual([{
      project_id: 'instrument-gain', runtime_id: 1,
    }])
    expect(plan.mappers).toEqual([{
      module_id: 'ControlMapper_1',
      source_owner_id: 'faders',
      source_module_id: 'FaderAdapter_1',
      target_module_id: 'Instrument_1',
      params: { bindings: [{
        binding_id: 1,
        source_device_id: 1,
        source_endpoint_id: 1,
        target_parameter_id: 1,
        source_kind: 0,
        mode: 0,
        curve: 2,
        pickup: 1,
        source_minimum: 0,
        source_maximum: 1,
        target_minimum: 0,
        target_maximum: 1,
        scale: 0.8,
        offset: 0.1,
        dead_zone: 0,
        quantization: 0,
        hysteresis: 0,
        pickup_tolerance: 0.02,
        initial_value: 0.8,
        invert: false,
      }] },
    }])
  })

  it('allocates IDs independently of project array order', () => {
    const first = compileControlPlan(workspaceWithControl(control))
    const reversed = structuredClone(control)
    reversed.devices.reverse()
    reversed.bindings.reverse()
    expect(compileControlPlan(workspaceWithControl(reversed))).toEqual(first)
  })

  it('rejects absent adapters and binding capacity overflow', () => {
    const missing = structuredClone(control)
    delete missing.devices[0].adapter_module_id
    expect(() => compileControlPlan(workspaceWithControl(missing)))
      .toThrow('requires a source owner with an adapter module')
    expect(() => compileControlPlan(workspaceWithControl(control), {
      maxBindingsPerMapper: 0,
    })).toThrow('exceeds its binding capacity')
  })

  it('materializes a native mapper module and positional target route', () => {
    const application = compileLaunchApplication(workspaceWithControl(control))
    const mapper = application.modules.find(
      (module) => module.module_class === 'MusicRaTControlMapper')!
    const source = application.modules.find(
      (module) => module.name === 'FaderAdapter_1')!
    const target = application.modules.find(
      (module) => module.name === 'Instrument_1')!

    expect(mapper.name).toBe('ControlMapper_1')
    expect(mapper.musicrat_generated).toBe('control_mapper')
    expect(mapper.inputs).toEqual([{
      source_system_id: source.outputs![0].system_id,
      source_instance_id: source.outputs![0].instance_id,
    }])
    expect(mapper.outputs).toHaveLength(1)
    expect(mapper.params).toEqual(compileControlPlan(workspaceWithControl(control))
      .mappers[0].params)
    expect(target.synced_inputs).toEqual([{
      source_system_id: mapper.outputs![0].system_id,
      source_instance_id: mapper.outputs![0].instance_id,
    }])
    expect(application.modules.indexOf(mapper)).toBeLessThan(
      application.modules.indexOf(target),
    )
  })

  it('injects compiled IDs into virtual control sources', () => {
    const workspace = workspaceWithControl(control)
    const source = workspace.modules.find((module) => module.id === 'FaderAdapter_1')!
    source.descriptor.module_class = 'MusicRaTControlSource'
    source.source.module_class = 'MusicRaTControlSource'
    source.source.params = {
      value: 0.125,
      gesture_flags: 2,
      enabled: true,
    }

    const application = compileLaunchApplication(workspace)
    expect(application.modules.find((module) => module.name === 'FaderAdapter_1')!.params)
      .toEqual({
        device_id: 1,
        endpoint_id: 1,
        origin_id: 1,
        kind: 0,
        value: 0.125,
        gesture_flags: 2,
        enabled: true,
      })
  })

  it('routes target parameter state back to the generated mapper', () => {
    const workspace = workspaceWithControl(control)

    const application = compileLaunchApplication(workspace)
    const mapper = application.modules.find(
      (module) => module.module_class === 'MusicRaTControlMapper')!
    const compiledTarget = application.modules.find(
      (module) => module.name === 'Instrument_1')!
    expect(mapper.synced_inputs).toEqual([{
      source_system_id: compiledTarget.outputs![1].system_id,
      source_instance_id: compiledTarget.outputs![1].instance_id,
    }])
  })

  it('materializes loop-suppressed feedback routing to an adapter', () => {
    const feedbackControl = structuredClone(control)
    feedbackControl.bindings[0].feedback = { owner_id: 'faders', endpoint_id: 'gain' }
    const workspace = workspaceWithControl(feedbackControl)
    const adapter = workspace.modules.find((module) => module.id === 'FaderAdapter_1')!
    adapter.descriptor.synced_inputs = ['CommRaT::Messages::ControlFeedbackBlock']
    adapter.descriptor.descriptor_metadata!.musicrat_ports!.ports.push({
      id: 'control_feedback',
      display_name: 'Control Feedback',
      direction: 'synced_input',
      port_index: 0,
      domain: 'control',
      required: false,
      max_connections: 1,
    })
    const plan = compileControlPlan(workspace)
    expect(plan.feedback_routers).toEqual([{
      module_id: 'ControlFeedbackRouter_1',
      target_module_id: 'Instrument_1',
      destination_module_ids: ['FaderAdapter_1'],
      params: { bindings: [{
        binding_id: 1,
        target_parameter_id: 1,
        destination_device_id: 1,
        destination_endpoint_id: 1,
        suppress_origin_id: 1,
      }] },
    }])

    const application = compileLaunchApplication(workspace)
    const router = application.modules.find(
      (module) => module.module_class === 'MusicRaTControlFeedbackRouter')!
    const compiledTarget = application.modules.find(
      (module) => module.name === 'Instrument_1')!
    const compiledAdapter = application.modules.find(
      (module) => module.name === 'FaderAdapter_1')!
    expect(router.musicrat_generated).toBe('control_feedback_router')
    expect(router.params).toEqual(plan.feedback_routers[0].params)
    expect(router.inputs).toEqual([{
      source_system_id: compiledTarget.outputs![1].system_id,
      source_instance_id: compiledTarget.outputs![1].instance_id,
    }])
    expect(compiledAdapter.synced_inputs).toEqual([{
      source_system_id: router.outputs![0].system_id,
      source_instance_id: router.outputs![0].instance_id,
    }])
    expect(application.modules.indexOf(router)).toBeLessThan(
      application.modules.indexOf(compiledTarget),
    )
  })

  it('materializes action bindings through a deck-control mapper', () => {
    const actionControl = structuredClone(control)
    actionControl.bindings = []
    actionControl.action_bindings = [{
      id: 'deck-rate',
      source: { owner_id: 'faders', endpoint_id: 'gain' },
      target: { module_id: 'Deck_1', action_id: 'set_rate' },
      quantization: 'beat',
      ramp_frames: 64,
    }]
    const workspace = workspaceWithControl(actionControl)
    workspace.modules.push({
      id: 'Deck_1',
      descriptor: {
        module_class: 'MusicRaTAudioFilePlayer',
        binary: '',
        synced_inputs: ['CommRaT::Messages::DeckControlEventBlock'],
        descriptor_metadata: {
          musicrat_ports: { schema_version: 1, ports: [{
            id: 'deck_control',
            display_name: 'Deck Control',
            direction: 'synced_input',
            port_index: 0,
            domain: 'control',
            required: false,
            max_connections: 1,
          }] },
          musicrat_actions: { schema_version: 1, actions: [{
            id: 'set_rate',
            display_name: 'Set Rate',
            group: 'Playback',
            input_port_id: 'deck_control',
            kind: 'continuous',
            event_type: 2,
            minimum: 0.5,
            maximum: 2,
            default_value: 1,
          }] },
        },
      },
      source: {
        name: 'Deck_1',
        module_class: 'MusicRaTAudioFilePlayer',
        synced_inputs: [],
      },
      position: { x: 0, y: 0 },
    })

    const plan = compileControlPlan(workspace)
    expect(plan.binding_ids).toEqual([{ project_id: 'deck-rate', runtime_id: 1 }])
    expect(plan.action_mappers).toEqual([{
      module_id: 'ActionMapper_1',
      source_owner_id: 'faders',
      source_module_id: 'FaderAdapter_1',
      target_module_id: 'Deck_1',
      target_port_id: 'deck_control',
      params: { bindings: [{
        binding_id: 1,
        source_device_id: 1,
        source_endpoint_id: 1,
        source_kind: 0,
        action_type: 2,
        quantization: 1,
        source_minimum: 0,
        source_maximum: 1,
        target_minimum: 0.5,
        target_maximum: 2,
        default_value: 1,
        ramp_frames: 64,
      }] },
    }])

    const application = compileLaunchApplication(workspace)
    const mapper = application.modules.find(
      (module) => module.module_class === 'MusicRaTActionMapper')!
    const source = application.modules.find(
      (module) => module.name === 'FaderAdapter_1')!
    const target = application.modules.find((module) => module.name === 'Deck_1')!
    expect(mapper.musicrat_generated).toBe('action_mapper')
    expect(mapper.inputs).toEqual([{
      source_system_id: source.outputs![0].system_id,
      source_instance_id: source.outputs![0].instance_id,
    }])
    expect(mapper.params).toEqual(plan.action_mappers[0].params)
    expect(target.synced_inputs).toEqual([{
      source_system_id: mapper.outputs![0].system_id,
      source_instance_id: mapper.outputs![0].instance_id,
    }])
    expect(application.modules.indexOf(mapper)).toBeLessThan(
      application.modules.indexOf(target),
    )
  })

  it('routes meter observations and external parameter state back to LVGL', () => {
    const observationControl = structuredClone(control)
    observationControl.bindings = [{
      id: 'hardware-gain',
      source: { owner_id: 'faders', endpoint_id: 'gain' },
      target: { module_id: 'Instrument_1', parameter_id: 1 },
      mode: 'absolute',
    }, {
      id: 'knob-gain',
      source: { owner_id: 'display-touch-display-1', endpoint_id: 'knob-gain' },
      target: { module_id: 'Instrument_2', parameter_id: 1 },
      mode: 'absolute',
    }]
    observationControl.deck = {
      schema_version: 1,
      panel: { width_mm: 320, height_mm: 140, thickness_mm: 3 },
      elements: [{
        id: 'touch-display-1',
        component_id: 'touch-display-5in',
        display_name: 'Main display',
        position: { x_mm: 0, y_mm: 0 },
      }],
    }
    observationControl.surfaces = [{
      id: 'display-touch-display-1',
      display_name: 'Main',
      target: 'lvgl',
      endpoints: [{
        id: 'slider-gain', display_name: 'First gain', direction: 'input', domain: 'unipolar',
      }, {
        id: 'knob-gain', display_name: 'Second gain', direction: 'input', domain: 'unipolar',
      }],
      widgets: [{
        id: 'slider-gain', kind: 'slider', display_name: 'First gain',
        layout: { x: 0, y: 0, width: 40, height: 10 }, route: 'control',
      }, {
        id: 'knob-gain', kind: 'knob', display_name: 'Second gain',
        layout: { x: 50, y: 0, width: 20, height: 20 }, route: 'control',
      }, {
        id: 'peak-meter',
        kind: 'meter',
        display_name: 'Peak',
        layout: { x: 0, y: 0, width: 20, height: 60 },
        route: 'feedback',
        observable: { module_id: 'Meter_1', observable_id: 'peak', channel: 0 },
        observation_binding_id: 'peak-observation',
      }, {
        id: 'gain-state',
        kind: 'knob',
        display_name: 'Gain state',
        layout: { x: 80, y: 0, width: 20, height: 10 },
        route: 'feedback',
        parameter: { module_id: 'Instrument_1', parameter_id: 1 },
      }],
    }]
    observationControl.observation_bindings = [{
      id: 'peak-observation',
      source: { module_id: 'Meter_1', observable_id: 'peak', channel: 0 },
      target: {
        surface_id: 'display-touch-display-1',
        widget_id: 'peak-meter',
        property: 'value',
      },
    }]
    const workspace = workspaceWithControl(observationControl)
    const secondInstrument = structuredClone(
      workspace.modules.find((module) => module.id === 'Instrument_1')!)
    secondInstrument.id = 'Instrument_2'
    secondInstrument.descriptor.descriptor_metadata!.musicrat_ports!.ports
      .find((port) => port.id === 'note_events')!.required = false
    secondInstrument.source = {
      ...secondInstrument.source,
      name: 'Instrument_2',
      outputs: [
        { system_id: 10, instance_id: 12 },
        { system_id: 20, instance_id: 12 },
      ],
      inputs: [],
      synced_inputs: [],
    }
    workspace.modules.push(secondInstrument)
    workspace.modules.push({
      id: 'Meter_1',
      descriptor: {
        module_class: 'MusicRaTLevelMeter',
        binary: '',
        outputs: [
          'CommRaT::Messages::AudioBlock',
          'CommRaT::Messages::LevelMeterBlock',
        ],
        descriptor_metadata: {
          musicrat_ports: { schema_version: 1, ports: [{
            id: 'levels', display_name: 'Levels', direction: 'output',
            port_index: 1, domain: 'telemetry', required: true, max_connections: 1,
          }] },
          musicrat_observables: { schema_version: 1, observables: [{
            id: 'peak', display_name: 'Peak', group: 'Levels', output_port_id: 'levels',
            selector: 'peak', kind: 'continuous', unit: 'linear',
            minimum: 0, maximum: 4, channel_selectable: true,
          }] },
        },
      },
      source: {
        name: 'Meter_1',
        module_class: 'MusicRaTLevelMeter',
        outputs: [
          { system_id: 10, instance_id: 8 },
          { system_id: 30, instance_id: 8 },
        ],
      },
      position: { x: 0, y: 0 },
    })

    const plan = compileControlPlan(workspace)
    expect(plan.observation_binding_ids).toEqual([{
      project_id: 'peak-observation', runtime_id: 1,
    }])
    expect(plan.observation_adapters).toEqual([{
      module_class: 'MusicRaTLevelMeterUiAdapter',
      module_id: 'LevelMeterUiAdapter_1',
      source_module_id: 'Meter_1',
      source_port_id: 'levels',
      surface_id: 'display-touch-display-1',
      params: { bindings: [{
        binding_id: 1,
        surface_id: 'display-touch-display-1',
        widget_id: 'peak-meter',
        property: 0,
        selector: 0,
        channel: 0,
      }] },
    }])
    expect(plan.parameter_state_adapters).toEqual([{
      module_id: 'ParameterStateUiAdapter_1',
      source_module_id: 'Instrument_1',
      surface_id: 'display-touch-display-1',
      params: { bindings: [{
        binding_id: 1,
        parameter_id: 1,
        surface_id: 'display-touch-display-1',
        widget_id: 'gain-state',
        parameter_minimum: 0,
        parameter_maximum: 1,
      }] },
    }])

    const application = compileLaunchApplication(workspace)
    const adapter = application.modules.find(
      (module) => module.module_class === 'MusicRaTLevelMeterUiAdapter')!
    const parameterAdapter = application.modules.find(
      (module) => module.module_class === 'MusicRaTParameterStateUiAdapter')!
    const meter = application.modules.find((module) => module.name === 'Meter_1')!
    const instrument = application.modules.find(
      (module) => module.name === 'Instrument_1')!
    expect(adapter.musicrat_generated).toBe('observation_adapter')
    expect(adapter.inputs).toEqual([{
      source_system_id: meter.outputs![1].system_id,
      source_instance_id: meter.outputs![1].instance_id,
    }])
    expect(adapter.params).toEqual(plan.observation_adapters[0].params)
    expect(application.modules.indexOf(adapter)).toBe(
      application.modules.indexOf(meter) + 1,
    )
    expect(parameterAdapter.musicrat_generated).toBe('parameter_state_ui_adapter')
    expect(parameterAdapter.inputs).toEqual([{
      source_system_id: instrument.outputs![1].system_id,
      source_instance_id: instrument.outputs![1].instance_id,
    }])
    expect(parameterAdapter.params).toEqual(plan.parameter_state_adapters[0].params)
    expect(application.modules.indexOf(parameterAdapter)).toBe(
      application.modules.indexOf(instrument) + 1,
    )
    const merger = application.modules.find(
      (module) => module.musicrat_generated === 'widget_update_merger')!
    const sink = application.modules.find(
      (module) => module.module_class === 'MusicRaTLvglWidgetSink')!
    const mappers = application.modules.filter(
      (module) => module.module_class === 'MusicRaTControlMapper')
    expect(sink.musicrat_generated).toBe('lvgl_widget_sink')
    expect(sink.outputs).toHaveLength(2)
    expect(sink.period_ms).toBe(10)
    expect(sink.synced_inputs).toEqual([{
      source_system_id: merger.outputs![0].system_id,
      source_instance_id: merger.outputs![0].instance_id,
    }])
    expect(merger.inputs).toEqual([{
      source_system_id: adapter.outputs![0].system_id,
      source_instance_id: adapter.outputs![0].instance_id,
    }])
    expect(merger.synced_inputs).toEqual([{
      source_system_id: parameterAdapter.outputs![0].system_id,
      source_instance_id: parameterAdapter.outputs![0].instance_id,
    }])
    expect(sink.params).toEqual({
      manifest_path: `${workspace.appName}.lvgl.json`,
      display_instance_id: 'touch-display-1',
      width: 800,
      height: 480,
      backend: 0,
      device_path: '',
      connector_id: -1,
      window_title: workspace.appName,
      device_id: 1,
      control_bindings: [{
        widget_id: 'slider-gain', endpoint_id: 2, origin_id: 2, kind: 0,
      }, {
        widget_id: 'knob-gain', endpoint_id: 1, origin_id: 1, kind: 0,
      }],
    })
    expect(mappers).toHaveLength(2)
    const hardwareSource = application.modules.find(
      (module) => module.name === 'FaderAdapter_1')!
    const hardwareTarget = application.modules.find(
      (module) => module.name === 'Instrument_1')!
    const displayTarget = application.modules.find(
      (module) => module.name === 'Instrument_2')!
    const hardwareMapper = mappers.find((candidate) => candidate.outputs?.[0].instance_id
      === hardwareTarget.synced_inputs?.[0].source_instance_id)!
    const displayMapper = mappers.find((candidate) => candidate.outputs?.[0].instance_id
      === displayTarget.synced_inputs?.[0].source_instance_id)!
    expect(hardwareMapper.inputs).toEqual([{
      source_system_id: hardwareSource.outputs![0].system_id,
      source_instance_id: hardwareSource.outputs![0].instance_id,
    }])
    expect(displayMapper.inputs).toEqual([{
      source_system_id: sink.outputs![0].system_id,
      source_instance_id: sink.outputs![0].instance_id,
    }])

    const imported = importApplication(
      application,
      workspace.modules.map((module) => module.descriptor),
    )
    const recompiled = compileLaunchApplication(imported)
    expect(recompiled.modules.filter(
      (module) => module.musicrat_generated === 'observation_adapter')).toHaveLength(1)
    expect(recompiled.modules.filter(
      (module) => module.musicrat_generated === 'parameter_state_ui_adapter')).toHaveLength(1)
    expect(recompiled.modules.filter(
      (module) => module.musicrat_generated === 'widget_update_merger')).toHaveLength(1)
    expect(recompiled.modules.filter(
      (module) => module.musicrat_generated === 'lvgl_widget_sink')).toHaveLength(1)

    const multipleStreams = structuredClone(workspace)
    const secondMeter = structuredClone(
      multipleStreams.modules.find((module) => module.id === 'Meter_1')!)
    secondMeter.id = 'Meter_2'
    secondMeter.source = {
      ...secondMeter.source,
      name: 'Meter_2',
      outputs: [
        { system_id: 10, instance_id: 9 },
        { system_id: 30, instance_id: 9 },
      ],
    }
    multipleStreams.modules.push(secondMeter)
    const multipleControl = multipleStreams.source.musicrat_control!
    multipleControl.surfaces[0].widgets.push({
      ...structuredClone(multipleControl.surfaces[0].widgets[0]),
      id: 'second-peak-meter',
      observable: { module_id: 'Meter_2', observable_id: 'peak', channel: 0 },
      observation_binding_id: 'second-peak-observation',
    })
    multipleControl.observation_bindings!.push({
      id: 'second-peak-observation',
      source: { module_id: 'Meter_2', observable_id: 'peak', channel: 0 },
      target: {
        surface_id: 'display-touch-display-1',
        widget_id: 'second-peak-meter',
        property: 'value',
      },
    })
    const mergedApplication = compileLaunchApplication(multipleStreams)
    const observationAdapters = mergedApplication.modules.filter(
      (module) => module.musicrat_generated === 'observation_adapter')
    const stateAdapter = mergedApplication.modules.find(
      (module) => module.musicrat_generated === 'parameter_state_ui_adapter')!
    const mergers = mergedApplication.modules.filter(
      (module) => module.musicrat_generated === 'widget_update_merger')!
    const mergedSink = mergedApplication.modules.find(
      (module) => module.musicrat_generated === 'lvgl_widget_sink')!
    expect(observationAdapters).toHaveLength(2)
    expect(mergers).toHaveLength(2)
    expect(mergers[0].inputs).toEqual([{
      source_system_id: observationAdapters[0].outputs![0].system_id,
      source_instance_id: observationAdapters[0].outputs![0].instance_id,
    }])
    expect(mergers[0].synced_inputs).toEqual([{
      source_system_id: observationAdapters[1].outputs![0].system_id,
      source_instance_id: observationAdapters[1].outputs![0].instance_id,
    }])
    expect(mergers[1].inputs).toEqual([{
      source_system_id: mergers[0].outputs![0].system_id,
      source_instance_id: mergers[0].outputs![0].instance_id,
    }])
    expect(mergers[1].synced_inputs).toEqual([{
      source_system_id: stateAdapter.outputs![0].system_id,
      source_instance_id: stateAdapter.outputs![0].instance_id,
    }])
    expect(mergedSink.synced_inputs).toEqual([{
      source_system_id: mergers[1].outputs![0].system_id,
      source_instance_id: mergers[1].outputs![0].instance_id,
    }])
    const recompiledMerged = compileLaunchApplication(importApplication(
      mergedApplication,
      multipleStreams.modules.map((module) => module.descriptor),
    ))
    expect(recompiledMerged.modules.filter(
      (module) => module.musicrat_generated === 'widget_update_merger')).toHaveLength(2)
    expect(recompiledMerged.modules.filter(
      (module) => module.musicrat_generated === 'lvgl_widget_sink')).toHaveLength(1)

    const multipleDisplays = structuredClone(workspace)
    multipleDisplays.source.musicrat_control!.deck!.elements.push({
      id: 'touch-display-2',
      component_id: 'display-3in',
      display_name: 'Secondary display',
      position: { x_mm: 130, y_mm: 0 },
    })
    multipleDisplays.source.musicrat_control!.surfaces.push({
      id: 'display-touch-display-2',
      display_name: 'Secondary',
      target: 'lvgl',
      endpoints: [],
      widgets: [{
        ...structuredClone(
          multipleDisplays.source.musicrat_control!.surfaces[0].widgets[0]),
        id: 'secondary-meter',
        observation_binding_id: 'secondary-observation',
      }],
    })
    multipleDisplays.source.musicrat_control!.observation_bindings!.push({
      id: 'secondary-observation',
      source: { module_id: 'Meter_1', observable_id: 'peak', channel: 0 },
      target: {
        surface_id: 'display-touch-display-2',
        widget_id: 'secondary-meter',
        property: 'value',
      },
    })
    const displayApplication = compileLaunchApplication(multipleDisplays)
    const displaySinks = displayApplication.modules.filter(
      (module) => module.musicrat_generated === 'lvgl_widget_sink')
    expect(displaySinks).toHaveLength(2)
    expect(displayApplication.modules.filter(
      (module) => module.musicrat_generated === 'widget_update_merger')).toHaveLength(1)
    expect(displaySinks.map((module) => module.params?.display_instance_id)).toEqual([
      'touch-display-1',
      'touch-display-2',
    ])
    expect(displaySinks[0].synced_inputs).not.toEqual(displaySinks[1].synced_inputs)
  })

  it('does not duplicate generated mappers after launch-artifact import', () => {
    const original = workspaceWithControl(control)
    const first = compileLaunchApplication(original)
    const imported = importApplication(
      first,
      [...demoDescriptors, original.modules[original.modules.length - 1].descriptor],
    )
    const second = compileLaunchApplication(imported)

    expect(second.modules.filter(
      (module) => module.musicrat_generated === 'control_mapper')).toHaveLength(1)
  })
})