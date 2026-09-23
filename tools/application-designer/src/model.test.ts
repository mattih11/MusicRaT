import { describe, expect, it } from 'vitest'
import { demoApplication, demoDescriptors } from './demo'
import { compileApplication, importApplication, validateConnection } from './model'

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
})