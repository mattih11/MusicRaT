import { describe, expect, it } from 'vitest'
import { demoApplication, demoDescriptors } from './demo'
import {
  compileApplication,
  importApplication,
  validateConnection,
  validateWorkspace,
  type MusicRaTControlProject,
} from './model'

const controlProject: MusicRaTControlProject = {
  schema_version: 1,
  devices: [{
    id: 'fader-bank',
    display_name: 'Fader Bank',
    kind: 'hardware',
    endpoints: [{
      id: 'fader-1',
      display_name: 'Fader 1',
      direction: 'input',
      domain: 'unipolar',
      minimum: 0,
      maximum: 1,
    }],
  }],
  bindings: [{
    id: 'instrument-gain',
    source: { owner_id: 'fader-bank', endpoint_id: 'fader-1' },
    target: { module_id: 'Instrument_1', parameter_id: 1 },
    mode: 'absolute',
    transform: { scale: 1, offset: 0, curve: 'linear' },
  }],
  surfaces: [{
    id: 'main-surface',
    display_name: 'Main Surface',
    target: 'ratgui',
    endpoints: [],
    widgets: [{
      id: 'gain-widget',
      kind: 'fader',
      display_name: 'Gain',
      layout: { x: 10, y: 10, width: 15, height: 60 },
      route: 'feedback',
      parameter: { module_id: 'Instrument_1', parameter_id: 1 },
      binding_id: 'instrument-gain',
    }],
  }],
}

describe('application graph compiler', () => {
  it('round-trips native CommRaT routes through stable port IDs', () => {
    const workspace = importApplication(demoApplication, demoDescriptors)
    const compiled = compileApplication(workspace)

    expect(workspace.connections[0].source_port_id).toBe('note_events')
    expect(workspace.connections[0].target_port_id).toBe('note_events')
    expect(compiled.modules[1].inputs).toEqual([
      { source_system_id: 10, source_instance_id: 1 },
    ])
    const panner = compiled.modules.find((module) => module.name === 'StereoPanner_1')!
    expect(compiled.modules[3].inputs).toEqual([
      {
        source_system_id: panner.outputs![0].system_id,
        source_instance_id: panner.outputs![0].instance_id,
      },
    ])
  })

  it('round-trips independent designer positions', () => {
    const application = structuredClone(demoApplication)
    application.musicrat_designer = {
      schema_version: 1,
      module_positions: { Instrument_1: { x: 420, y: 240 } },
      binding_positions: { 'module:Instrument_1': { x: 760, y: 180 } },
    }

    const workspace = importApplication(application, demoDescriptors)
    expect(workspace.modules.find((module) => module.id === 'Instrument_1')?.position)
      .toEqual({ x: 420, y: 240 })
    expect(compileApplication(workspace).musicrat_designer).toEqual({
      schema_version: 1,
      module_positions: Object.fromEntries(workspace.modules.map((module) => [
        module.id,
        module.position,
      ])),
      binding_positions: { 'module:Instrument_1': { x: 760, y: 180 } },
    })
  })

  it('rejects a note output connected to an audio input', () => {
    const workspace = importApplication(demoApplication, demoDescriptors)
    const invalid = {
      id: 'invalid',
      source_module_id: 'Keyboard_1',
      source_port_id: 'note_events',
      target_module_id: 'StereoPanner_1',
      target_port_id: 'audio_in',
    }

    expect(validateConnection(workspace.modules, invalid)).toEqual([
      { target: 'invalid', message: 'note cannot connect to audio.' },
    ])
  })

  it('preserves text startup parameters in CommRaT JSON', () => {
    const workspace = importApplication(demoApplication, demoDescriptors)
    const sink = workspace.modules.find((module) => module.id === 'WavSink_1')!
    sink.source.params = { ...sink.source.params, path: '/tmp/rendered mix.wav' }

    const compiled = compileApplication(workspace)

    expect(compiled.modules.find((module) => module.name === 'WavSink_1')?.params?.path)
      .toBe('/tmp/rendered mix.wav')
  })

  it('serializes incomplete drafts without weakening launch-ready export', () => {
    const workspace = importApplication(demoApplication, demoDescriptors)
    workspace.connections = workspace.connections.filter((connection) =>
      connection.target_module_id !== 'WavSink_1')

    expect(() => compileApplication(workspace)).toThrow('Audio In requires a connection.')
    expect(compileApplication(workspace, { validate: false }).modules).toHaveLength(5)
  })

  it('round-trips the schema-versioned control project extension', () => {
    const application = structuredClone(demoApplication)
    application.musicrat_control = controlProject

    const workspace = importApplication(application, demoDescriptors)
    expect(validateWorkspace(workspace)).toEqual([])
    expect(compileApplication(workspace).musicrat_control).toEqual(controlProject)
  })

  it('rejects unresolved control references', () => {
    const application = structuredClone(demoApplication)
    application.musicrat_control = structuredClone(controlProject)
    application.musicrat_control.bindings[0].source.endpoint_id = 'missing'

    const issues = validateWorkspace(importApplication(application, demoDescriptors))
    expect(issues).toContainEqual({
      target: 'instrument-gain',
      message: 'Binding source endpoint does not exist.',
    })
  })

  it('requires parameter and action binding IDs to share one unique namespace', () => {
    const application = structuredClone(demoApplication)
    application.musicrat_control = structuredClone(controlProject)
    application.musicrat_control.action_bindings = [{
      id: 'instrument-gain',
      source: { owner_id: 'fader-bank', endpoint_id: 'fader-1' },
      target: { module_id: 'Instrument_1', action_id: 'missing' },
      quantization: 'immediate',
    }]

    expect(validateWorkspace(importApplication(application, demoDescriptors))).toContainEqual({
      target: 'instrument-gain',
      message: 'Parameter and action binding IDs must be nonempty and unique.',
    })
  })

  it('rejects a missing device adapter module', () => {
    const application = structuredClone(demoApplication)
    application.musicrat_control!.devices[0].adapter_module_id = 'MissingAdapter_1'

    expect(validateWorkspace(importApplication(application, demoDescriptors))).toContainEqual({
      target: 'virtual-controls',
      message: 'Device adapter module MissingAdapter_1 does not exist.',
    })
  })

  it('validates endpoint capabilities against direction and domain', () => {
    const application = structuredClone(demoApplication)
    const endpoint = application.musicrat_control!.devices[0].endpoints[0]
    endpoint.capabilities = ['motorized']

    expect(validateWorkspace(importApplication(application, demoDescriptors))).toContainEqual({
      target: 'gain',
      message: 'Motorized endpoints must be bidirectional and feedback-capable.',
    })

    endpoint.direction = 'bidirectional'
    endpoint.capabilities = ['feedback', 'motorized']
    expect(validateWorkspace(importApplication(application, demoDescriptors))).toEqual([])
  })

  it('validates and round-trips an authoritative physical deck', () => {
    const application = structuredClone(demoApplication)
    const control = application.musicrat_control!
    control.deck = {
      schema_version: 1,
      panel: { width_mm: 300, height_mm: 120, thickness_mm: 3 },
      elements: [{
        id: 'gain-fader',
        component_id: 'fader-60mm',
        display_name: 'Gain',
        position: { x_mm: 20, y_mm: 10 },
      }],
    }

    const workspace = importApplication(application, demoDescriptors)
    expect(validateWorkspace(workspace)).toEqual([])
    expect(compileApplication(workspace).musicrat_control?.deck).toEqual(control.deck)

    control.deck!.elements[0].position.x_mm = -1
    expect(validateWorkspace(importApplication(application, demoDescriptors))).toContainEqual({
      target: 'gain-fader',
      message: 'Deck component must fit within the panel.',
    })
  })
})