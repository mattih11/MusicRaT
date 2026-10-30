import { describe, expect, it } from 'vitest'
import { connectKeyboard, disconnectKeyboardEdges, projectKeyboardGraph } from './keyboard-graph-model'
import { compileLaunchApplication } from './control-compiler'
import {
  compileApplication,
  importApplication,
  keyboardNoteTargets,
  noteInputTargets,
  validateWorkspace,
  type DesignerModule,
  type DesignerWorkspace,
} from './model'

function synth(id: string): DesignerModule {
  return {
    id,
    source: { name: id, module_class: 'Synth' },
    position: { x: 80, y: 80 },
    descriptor: {
      module_class: 'Synth',
      binary: '',
      synced_inputs: ['CommRaT::Messages::ParameterEventBlock', 'CommRaT::Messages::NoteEventBlock'],
      descriptor_metadata: { musicrat_ports: { schema_version: 1, ports: [
        { id: 'parameters', display_name: 'Parameters', direction: 'synced_input',
          domain: 'parameter', port_index: 0, required: false, max_connections: 1 },
        { id: 'notes', display_name: 'Notes', direction: 'synced_input',
          domain: 'note', port_index: 1, required: true, max_connections: 1 },
      ] } },
    },
  }
}

function fixture(): DesignerWorkspace {
  return {
    appName: 'KeyboardGraph',
    modules: [synth('ThreeOscillator_1'), synth('WavetableOscillator_1')],
    connections: [],
    source: {
      app_name: 'KeyboardGraph',
      modules: [],
      musicrat_control: {
        schema_version: 1, devices: [], bindings: [],
        deck: {
          schema_version: 1,
          panel: { width_mm: 160, height_mm: 90, thickness_mm: 3 },
          elements: [{ id: 'screen', component_id: 'touch-display-5in',
            display_name: 'Screen', position: { x_mm: 0, y_mm: 0 } }],
        },
        surfaces: [{
          id: 'display-screen', display_name: 'Screen', target: 'lvgl', endpoints: [],
          widgets: [{ id: 'keys', kind: 'keyboard', display_name: 'Keys',
            layout: { x: 0, y: 0, width: 100, height: 100 }, route: 'control' }],
        }],
      },
    },
  }
}

function connectBoth(workspace: DesignerWorkspace) {
  for (const target of noteInputTargets(workspace.modules)) {
    workspace.source.musicrat_control = connectKeyboard(
      workspace, projectKeyboardGraph(workspace).nodes[0].id, target)
  }
}

describe('keyboard routing graph', () => {
  it('projects unrouted LVGL widgets, excluding other surfaces and other widget types', () => {
    const workspace = fixture()
    const control = workspace.source.musicrat_control!
    control.surfaces.push({ ...structuredClone(control.surfaces[0]), id: 'generic', target: 'generic' })
    control.surfaces[0].widgets.push({ ...control.surfaces[0].widgets[0], id: 'slider', kind: 'slider' })
    const graph = projectKeyboardGraph(workspace)
    expect(graph.nodes).toHaveLength(1)
    expect(graph.edges).toEqual([])
    expect(graph.nodes[0].data.display_name).toBe('Keys')
    expect(graph.nodes[0].deletable).toBe(false)
  })

  it('connects a keyboard to both synths and compiles exact note-input positions', () => {
    const workspace = fixture()
    connectBoth(workspace)
    expect(validateWorkspace(workspace)).toEqual([])
    expect(projectKeyboardGraph(workspace).edges).toHaveLength(2)
    const application = compileLaunchApplication(workspace)
    const sink = application.modules.find((module) => module.musicrat_generated === 'lvgl_widget_sink')!
    for (const target of application.modules.filter((module) => module.module_class === 'Synth')) {
      expect(target.synced_inputs?.[1]).toEqual({
        source_system_id: sink.outputs![1].system_id,
        source_instance_id: sink.outputs![1].instance_id,
      })
    }
    expect(application.modules.filter((module) => module.musicrat_generated === 'lvgl_widget_sink'))
      .toHaveLength(1)
    expect(application.modules.some((module) => module.module_class === 'Keyboard')).toBe(false)
  })

  it('preserves cables and keyboard positions on draft save and launch export/reimport', () => {
    const workspace = fixture()
    connectBoth(workspace)
    const id = projectKeyboardGraph(workspace).nodes[0].id
    workspace.source.musicrat_designer = {
      schema_version: 1, keyboard_positions: { [id]: { x: 10, y: 40 } },
    }
    for (const document of [compileApplication(workspace), compileLaunchApplication(workspace)]) {
      const imported = importApplication(document, [workspace.modules[0].descriptor])
      expect(projectKeyboardGraph(imported)).toEqual(projectKeyboardGraph(workspace))
      expect(imported.connections).toEqual([])
      expect(imported.modules).toHaveLength(2)
    }
  })

  it('deletes only the selected cable and preserves other destinations', () => {
    const workspace = fixture()
    connectBoth(workspace)
    const graph = projectKeyboardGraph(workspace)
    workspace.source.musicrat_control = disconnectKeyboardEdges(workspace, [graph.edges[0].id])
    expect(projectKeyboardGraph(workspace).edges).toEqual([graph.edges[1]])
    expect(validateWorkspace(workspace)).toEqual([{
      target: 'ThreeOscillator_1', message: 'Notes requires a connection.',
    }])
  })

  it('shows and disconnects legacy dropdown routes', () => {
    const workspace = fixture()
    const widget = workspace.source.musicrat_control!.surfaces[0].widgets[0]
    widget.note_target_module_id = 'ThreeOscillator_1'
    expect(keyboardNoteTargets(widget, workspace.modules)).toEqual([
      { module_id: 'ThreeOscillator_1', port_id: 'notes' },
    ])
    const edge = projectKeyboardGraph(workspace).edges[0]
    workspace.source.musicrat_control = disconnectKeyboardEdges(workspace, [edge.id])
    expect(projectKeyboardGraph(workspace).edges).toEqual([])
  })

  it('rejects audio, wrong payloads, duplicate cables, and physical route conflicts', () => {
    const workspace = fixture()
    const id = projectKeyboardGraph(workspace).nodes[0].id
    expect(() => connectKeyboard(workspace, id, { module_id: 'ThreeOscillator_1', port_id: 'parameters' }))
      .toThrow('compatible note-event')
    workspace.modules[0].descriptor.synced_inputs![1] = 'OtherNotePayload'
    expect(noteInputTargets(workspace.modules).map((target) => target.module_id)).toEqual(['WavetableOscillator_1'])
    workspace.modules[0].descriptor.synced_inputs![1] = 'CommRaT::Messages::NoteEventBlock'
    connectBoth(workspace)
    expect(() => connectKeyboard(workspace, id, { module_id: 'ThreeOscillator_1', port_id: 'notes' }))
      .toThrow('already has a connection')
    workspace.connections.push({
      id: 'physical', source_module_id: 'Missing', source_port_id: 'notes',
      target_module_id: 'ThreeOscillator_1', target_port_id: 'notes',
    })
    expect(validateWorkspace(workspace).some((issue) => issue.message.includes('already routed'))).toBe(true)
    expect(() => compileLaunchApplication(workspace)).toThrow()
  })

  it('fails explicitly for stale targets and a routed keyboard without a hardware display', () => {
    const workspace = fixture()
    connectBoth(workspace)
    workspace.modules.pop()
    expect(validateWorkspace(workspace).some((issue) => issue.message.includes('missing or incompatible'))).toBe(true)
    expect(() => compileLaunchApplication(workspace)).toThrow('missing or incompatible')
    workspace.modules.push(synth('WavetableOscillator_1'))
    workspace.source.musicrat_control!.deck = undefined
    expect(() => compileLaunchApplication(workspace)).toThrow('requires an LVGL hardware display')
  })
})
