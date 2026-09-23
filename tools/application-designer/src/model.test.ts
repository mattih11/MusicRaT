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
    expect(compiled.modules[3].inputs).toEqual([
      { source_system_id: 10, source_instance_id: 3 },
    ])
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
    expect(compileApplication(workspace, { validate: false }).modules).toHaveLength(4)
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
})