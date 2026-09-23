import { describe, expect, it } from 'vitest'
import { compileControlPlan, compileLaunchApplication } from './control-compiler'
import { demoApplication, demoDescriptors } from './demo'
import { importApplication, type MusicRaTControlProject } from './model'

function workspaceWithControl(control: MusicRaTControlProject) {
  const workspace = importApplication(demoApplication, demoDescriptors)
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
    const target = application.modules.find(
      (module) => module.name === 'Instrument_1')!

    expect(mapper.name).toBe('ControlMapper_1')
    expect(mapper.musicrat_generated).toBe('control_mapper')
    expect(mapper.inputs).toEqual([{
      source_system_id: 10,
      source_instance_id: 4,
    }])
    expect(mapper.outputs).toEqual([{ system_id: 10, instance_id: 5 }])
    expect(mapper.params).toEqual(compileControlPlan(workspaceWithControl(control))
      .mappers[0].params)
    expect(target.synced_inputs).toEqual([{
      source_system_id: 10,
      source_instance_id: 5,
    }])
    expect(application.modules.indexOf(mapper)).toBeLessThan(
      application.modules.indexOf(target),
    )
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