import { describe, expect, it } from 'vitest'
import { compileControlPlan, compileLaunchApplication } from './control-compiler'
import { demoApplication, demoDescriptors } from './demo'
import { importApplication, type MusicRaTControlProject } from './model'

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

  it('materializes LevelMeter observations through a typed UI adapter', () => {
    const observationControl = structuredClone(control)
    observationControl.bindings = [{
      id: 'slider-gain',
      source: { owner_id: 'display-touch-display-1', endpoint_id: 'slider-gain' },
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

    const application = compileLaunchApplication(workspace)
    const adapter = application.modules.find(
      (module) => module.module_class === 'MusicRaTLevelMeterUiAdapter')!
    const meter = application.modules.find((module) => module.name === 'Meter_1')!
    expect(adapter.musicrat_generated).toBe('observation_adapter')
    expect(adapter.inputs).toEqual([{
      source_system_id: meter.outputs![1].system_id,
      source_instance_id: meter.outputs![1].instance_id,
    }])
    expect(adapter.params).toEqual(plan.observation_adapters[0].params)
    expect(application.modules.indexOf(adapter)).toBe(
      application.modules.indexOf(meter) + 1,
    )
    const sink = application.modules.find(
      (module) => module.module_class === 'MusicRaTLvglWidgetSink')!
    const mappers = application.modules.filter(
      (module) => module.module_class === 'MusicRaTControlMapper')
    expect(sink.musicrat_generated).toBe('lvgl_widget_sink')
    expect(sink.outputs).toHaveLength(1)
    expect(sink.period_ms).toBe(10)
    expect(sink.synced_inputs).toEqual([{
      source_system_id: adapter.outputs![0].system_id,
      source_instance_id: adapter.outputs![0].instance_id,
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
    expect(mappers.every((mapper) => mapper.inputs?.[0].source_system_id
      === sink.outputs?.[0].system_id
      && mapper.inputs?.[0].source_instance_id === sink.outputs?.[0].instance_id)).toBe(true)
    for (const targetName of ['Instrument_1', 'Instrument_2']) {
      const target = application.modules.find((module) => module.name === targetName)!
      const mapper = mappers.find((candidate) => candidate.outputs?.[0].instance_id
        === target.synced_inputs?.[0].source_instance_id)!
      expect(mapper).toBeDefined()
    }

    const imported = importApplication(
      application,
      workspace.modules.map((module) => module.descriptor),
    )
    const recompiled = compileLaunchApplication(imported)
    expect(recompiled.modules.filter(
      (module) => module.musicrat_generated === 'observation_adapter')).toHaveLength(1)
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
    const adapters = mergedApplication.modules.filter(
      (module) => module.musicrat_generated === 'observation_adapter')
    const merger = mergedApplication.modules.find(
      (module) => module.musicrat_generated === 'widget_update_merger')!
    const mergedSink = mergedApplication.modules.find(
      (module) => module.musicrat_generated === 'lvgl_widget_sink')!
    expect(adapters).toHaveLength(2)
    expect(merger.inputs).toEqual([{
      source_system_id: adapters[0].outputs![0].system_id,
      source_instance_id: adapters[0].outputs![0].instance_id,
    }])
    expect(merger.synced_inputs).toEqual([{
      source_system_id: adapters[1].outputs![0].system_id,
      source_instance_id: adapters[1].outputs![0].instance_id,
    }])
    expect(mergedSink.synced_inputs).toEqual([{
      source_system_id: merger.outputs![0].system_id,
      source_instance_id: merger.outputs![0].instance_id,
    }])
    const recompiledMerged = compileLaunchApplication(importApplication(
      mergedApplication,
      multipleStreams.modules.map((module) => module.descriptor),
    ))
    expect(recompiledMerged.modules.filter(
      (module) => module.musicrat_generated === 'widget_update_merger')).toHaveLength(1)
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
      (module) => module.musicrat_generated === 'widget_update_merger')).toHaveLength(0)
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