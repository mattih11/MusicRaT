import { describe, expect, it } from 'vitest'
import { compileLaunchApplication } from './control-compiler'
import { demoApplication, demoDescriptors } from './demo'
import { actionTargets, parameterTargets } from './graph-targets'
import {
  compileApplication, importApplication, validateWorkspace,
  type DesignerModule, type DesignerWorkspace,
} from './model'
import {
  connectGraph, disconnectGraph, graphModuleId, moveGraphNodes, projectUnifiedGraph,
  removeGraphModules, type GraphNode, type GraphPort,
} from './unified-graph-model'
import { addSurfaceWidget } from './ui-designer-model'
import { uiComponentDefinition } from './ui-component-catalog'

function fixture() {
  const workspace = importApplication(structuredClone(demoApplication), structuredClone(demoDescriptors))
  const control = workspace.source.musicrat_control!
  control.devices[0].kind = 'hardware'
  control.devices[0].endpoints[0].direction = 'bidirectional'
  control.devices[0].endpoints[0].capabilities = ['absolute', 'feedback']
  control.deck = { schema_version: 1,
    panel: { width_mm: 160, height_mm: 90, thickness_mm: 3 },
    elements: [{ id: 'screen', component_id: 'touch-display-5in', display_name: 'Screen',
      position: { x_mm: 0, y_mm: 0 } }],
  }
  control.surfaces.push({ id: 'display-screen', display_name: 'Screen',
    target: 'lvgl', endpoints: [], widgets: [] })
  for (const kind of ['slider', 'meter', 'keyboard', 'button'] as const) {
    workspace.source.musicrat_control = addSurfaceWidget(workspace.source.musicrat_control!,
      'display-screen', uiComponentDefinition(kind)!).control
  }
  workspace.modules.push({
    id: 'NoteSynth', source: { name: 'NoteSynth', module_class: 'NoteSynth' },
    position: { x: 400, y: 500 },
    descriptor: { module_class: 'NoteSynth', binary: '',
      outputs: ['CommRaT::Messages::AudioBlock'], synced_inputs: ['CommRaT::Messages::NoteEventBlock'],
      descriptor_metadata: { musicrat_ports: { schema_version: 1, ports: [
        { id: 'notes', display_name: 'Notes', direction: 'synced_input',
          port_index: 0, domain: 'note', required: false, max_connections: 1 },
        { id: 'audio', display_name: 'Audio', direction: 'output',
          port_index: 0, domain: 'audio', required: false, max_connections: 1 },
      ] } },
    },
  })
  return workspace
}

function socket(workspace: DesignerWorkspace, direction: 'inputs' | 'outputs',
  predicate: (port: GraphPort, node: GraphNode) => boolean) {
  const projection = projectUnifiedGraph(workspace)
  for (const node of projection.nodes) {
    const port = node.data[direction].find((port) => predicate(port, node))
    if (port) return { node, port }
  }
  throw new Error('Test socket not found.')
}

function connect(workspace: DesignerWorkspace,
  from: (port: GraphPort, node: GraphNode) => boolean,
  to: (port: GraphPort, node: GraphNode) => boolean) {
  const source = socket(workspace, 'outputs', from)
  const target = socket(workspace, 'inputs', to)
  return connectGraph(workspace, projectUnifiedGraph(workspace), {
    source: source.node.id, sourceHandle: source.port.id,
    target: target.node.id, targetHandle: target.port.id,
  }).workspace
}
const uiSlider = (port: GraphPort, node: GraphNode) => node.data.widget_id === 'slider-1' && port.socket.kind === 'control'
const parameter = (port: GraphPort) => port.socket.kind === 'parameter' && port.socket.module_id === 'Instrument_1'
const state = (port: GraphPort) => port.socket.kind === 'state' && port.socket.module_id === 'Instrument_1'
const sliderFeedback = (port: GraphPort, node: GraphNode) => node.data.widget_id === 'slider-1'
  && port.socket.kind === 'widget-feedback'
const hardware = (port: GraphPort, node: GraphNode) => node.data.kind === 'hardware' && port.socket.kind === 'control'
const hardwareFeedback = (port: GraphPort, node: GraphNode) => node.data.kind === 'hardware' && port.socket.kind === 'feedback'

describe('unified application graph', () => {
  it('uses collision-free IDs safe for React Flow attribute selectors and dynamic handle updates', () => {
    const workspace = fixture()
    workspace.modules[0].id = 'Module ["quoted"] / name'
    const projection = projectUnifiedGraph(workspace, { showImplementation: true })
    const ids = projection.nodes.flatMap((node) => [node.id,
      ...node.data.inputs.map((port) => port.id), ...node.data.outputs.map((port) => port.id)])
    expect(new Set(projection.nodes.map((node) => node.id)).size).toBe(projection.nodes.length)
    for (const id of ids) expect(id).not.toMatch(/["\\\s<>]/)
    expect(graphModuleId('a,b')).not.toBe(graphModuleId('a", "b'))
  })

  it('shows real modules, individually colored UI/hardware elements and only automatable parameter targets', () => {
    const workspace = fixture()
    const projection = projectUnifiedGraph(workspace)
    expect(projection.nodes.some((node) => node.data.module_id === 'ControlSource_1')).toBe(false)
    expect(projection.nodes.filter((node) => node.data.kind === 'ui')).toHaveLength(4)
    expect(projection.nodes.filter((node) => node.data.kind === 'hardware')).toHaveLength(1)
    const instrument = projection.nodes.find((node) => node.id === graphModuleId('Instrument_1'))!
    expect(instrument.data.inputs.some((port) => port.socket.kind === 'physical' && port.domain === 'note')).toBe(true)
    expect(instrument.data.inputs.filter((port) => port.socket.kind === 'parameter')).toHaveLength(1)
    expect(instrument.data.outputs.filter((port) => port.socket.kind === 'state')).toHaveLength(1)
    workspace.modules[1].descriptor.descriptor_metadata!.musicrat_parameters!.parameters[0].automatable = false
    expect(parameterTargets(workspace.modules).some((target) => target.module.id === 'Instrument_1')).toBe(false)
    expect(projectUnifiedGraph(workspace).nodes.find((node) => node.id === instrument.id)!.data.startup_parameters).toBe(1)
  })

  it('links a UI slider to a parameter and compiles hidden mapper + LVGL state feedback automatically', () => {
    const workspace = connect(fixture(), uiSlider, parameter)
    const binding = workspace.source.musicrat_control!.bindings[0]
    expect(binding.target).toEqual({ module_id: 'Instrument_1', parameter_id: 1 })
    expect(binding.mode).toBe('absolute')
    expect(projectUnifiedGraph(workspace).edges.filter((edge) =>
      ['binding', 'feedback'].includes(edge.data!.route.kind))).toHaveLength(2)
    expect(validateWorkspace(workspace)).toEqual([])
    const launch = compileLaunchApplication(workspace)
    expect(launch.modules[0].musicrat_generated).toBe('lvgl_widget_sink')
    expect(launch.modules.some((module) => module.musicrat_generated === 'control_mapper')).toBe(true)
    expect(launch.modules.some((module) => module.musicrat_generated === 'parameter_state_ui_adapter')).toBe(true)
    expect(launch.modules.some((module) => module.musicrat_generated === 'lvgl_widget_sink')).toBe(true)
    expect(launch.modules.some((module) => module.musicrat_generated === 'control_feedback_router')).toBe(false)
  })

  it('compiles choice controls using actual choice values and unnormalized state feedback', () => {
    const workspace = fixture()
    const instrument = workspace.modules.find((module) => module.id === 'Instrument_1')!
    const descriptor = instrument.descriptor.descriptor_metadata!.musicrat_parameters!.parameters[0]
    descriptor.kind = 'choice'
    descriptor.choices = [{ value: 10, label: 'Clean' }, { value: 20, label: 'Drive' }]
    const added = addSurfaceWidget(workspace.source.musicrat_control!, 'display-screen', uiComponentDefinition('choice')!)
    workspace.source.musicrat_control = added.control
    const linked = connect(workspace, (port, node) => node.data.widget_id === added.widgetId
      && port.socket.kind === 'control', parameter)
    const launch = compileLaunchApplication(linked)
    const mapper = launch.modules.find((module) => module.musicrat_generated === 'control_mapper')!
    expect(mapper.params!.bindings).toEqual([expect.objectContaining({
      source_minimum: 10, source_maximum: 20, target_minimum: 10, target_maximum: 20,
    })])
    const adapter = launch.modules.find((module) => module.musicrat_generated === 'parameter_state_ui_adapter')!
    expect(adapter.params!.bindings).toEqual([expect.objectContaining({ value_kind: 2 })])
  })

  it('can remove and reconnect UI feedback independently without dropping the control binding', () => {
    let workspace = connect(fixture(), uiSlider, parameter)
    const feedback = projectUnifiedGraph(workspace).edges.find((edge) => edge.data?.route.kind === 'feedback')!
    workspace = disconnectGraph(workspace, [feedback.data!.route])
    expect(workspace.source.musicrat_control!.bindings[0].feedback).toBeUndefined()
    expect(workspace.source.musicrat_control!.surfaces[0].widgets[0].parameter_feedback).toBe(false)
    expect(projectUnifiedGraph(workspace).edges.some((edge) =>
      ['feedback', 'widget-state'].includes(edge.data!.route.kind))).toBe(false)
    expect(compileLaunchApplication(workspace).modules.some((module) =>
      module.musicrat_generated === 'parameter_state_ui_adapter')).toBe(false)
    workspace = connect(workspace, state, sliderFeedback)
    expect(projectUnifiedGraph(workspace).edges.some((edge) => edge.data?.route.kind === 'widget-state')).toBe(true)
    expect(compileLaunchApplication(workspace).modules.some((module) =>
      module.musicrat_generated === 'parameter_state_ui_adapter')).toBe(true)
  })

  it('deletes a control cable without silently deleting its independent UI feedback', () => {
    let workspace = connect(fixture(), uiSlider, parameter)
    const binding = projectUnifiedGraph(workspace).edges.find((edge) => edge.data?.route.kind === 'binding')!
    workspace = disconnectGraph(workspace, [binding.data!.route])
    expect(workspace.source.musicrat_control!.bindings).toEqual([])
    expect(workspace.source.musicrat_control!.surfaces[0].widgets[0].route).toBe('feedback')
    expect(projectUnifiedGraph(workspace).edges.some((edge) => edge.data?.route.kind === 'widget-state')).toBe(true)
    expect(() => compileLaunchApplication(workspace)).not.toThrow()
    workspace = connect(workspace, uiSlider, parameter)
    expect(projectUnifiedGraph(workspace).edges.filter((edge) =>
      ['binding', 'feedback'].includes(edge.data!.route.kind))).toHaveLength(2)
    expect(compileLaunchApplication(workspace).modules.some((module) =>
      module.musicrat_generated === 'parameter_state_ui_adapter')).toBe(true)
  })

  it('rejects control connections that would silently replace independent parameter feedback', () => {
    const workspace = connect(fixture(), state, sliderFeedback)
    const other = structuredClone(workspace.modules.find((module) => module.id === 'Instrument_1')!)
    other.id = 'OtherInstrument'
    other.source.name = other.id
    workspace.modules.push(other)
    const original = structuredClone(workspace)
    expect(() => connect(workspace, uiSlider, (port) => port.socket.kind === 'parameter'
      && port.socket.module_id === other.id)).toThrow('Disconnect the widget feedback')
    expect(workspace).toEqual(original)
  })

  it('does not add feedback for targets without state outputs', () => {
    const workspace = fixture()
    const instrument = workspace.modules.find((module) => module.id === 'Instrument_1')!
    instrument.descriptor.descriptor_metadata!.musicrat_ports!.ports =
      instrument.descriptor.descriptor_metadata!.musicrat_ports!.ports.filter((port) => port.id !== 'parameter_state')
    const linked = connect(workspace, uiSlider, parameter)
    expect(linked.source.musicrat_control!.bindings[0].feedback).toBeUndefined()
    expect(linked.source.musicrat_control!.surfaces[0].widgets[0].parameter_feedback).toBe(false)
    expect(() => compileLaunchApplication(linked)).not.toThrow()
  })

  it('links hardware control and reverse state feedback, generating a feedback router', () => {
    const linked = connect(connect(fixture(), hardware, parameter), state, hardwareFeedback)
    expect(projectUnifiedGraph(linked).edges.some((edge) => edge.data?.route.kind === 'feedback')).toBe(true)
    expect(compileLaunchApplication(linked).modules.some((module) =>
      module.musicrat_generated === 'control_feedback_router')).toBe(true)
  })

  it('preserves raw-control mode semantics, rejects incompatible/duplicate connections', () => {
    const workspace = fixture()
    const endpoint = workspace.source.musicrat_control!.devices[0].endpoints[0]
    endpoint.domain = 'relative'
    endpoint.capabilities = ['relative', 'feedback']
    const linked = connect(workspace, hardware, parameter)
    expect(linked.source.musicrat_control!.bindings[0].mode).toBe('relative')
    expect(() => connect(linked, hardware, parameter)).toThrow('already linked')
    expect(() => connect(workspace, hardware, (port) => port.socket.kind === 'physical' && port.domain === 'audio'))
      .toThrow('Incompatible cable')
  })

  it('edits audio and note routes on the same graph and rejects input conflicts', () => {
    let workspace = fixture()
    workspace = connect(workspace, (port) => port.socket.kind === 'keyboard',
      (port) => port.socket.kind === 'physical' && port.socket.module_id === 'NoteSynth' && port.domain === 'note')
    const notes = projectUnifiedGraph(workspace).edges.find((edge) => edge.data?.route.kind === 'keyboard')!
    expect(() => connect(workspace, (port) => port.socket.kind === 'keyboard',
      (port) => port.socket.kind === 'physical' && port.socket.module_id === 'NoteSynth' && port.domain === 'note'))
      .toThrow('already has')
    expect(() => connect(workspace, (port) => port.socket.kind === 'physical' && port.socket.module_id === 'NoteSynth',
      (port) => port.socket.kind === 'physical' && port.socket.module_id === 'NoteSynth' && port.domain === 'note'))
      .toThrow('audio cannot connect to note')
    workspace = disconnectGraph(workspace, [notes.data!.route])
    expect(projectUnifiedGraph(workspace).edges.some((edge) => edge.data?.route.kind === 'keyboard')).toBe(false)
    expect(() => compileLaunchApplication(workspace)).not.toThrow()
  })

  it('persists the unified graph and semantic references through draft and launch round trips', () => {
    let workspace = connect(fixture(), uiSlider, parameter)
    const node = projectUnifiedGraph(workspace).nodes.find((item) => item.data.widget_id === 'slider-1')!
    workspace = moveGraphNodes(workspace, {
      [node.id]: { x: 13, y: 17 }, [graphModuleId('Instrument_1')]: { x: 500, y: 123 },
    })
    const descriptors = workspace.modules.map((module) => module.descriptor)
    for (const document of [compileApplication(workspace), compileLaunchApplication(workspace)]) {
      const reopened = importApplication(document, descriptors)
      expect(projectUnifiedGraph(reopened)).toEqual(projectUnifiedGraph(workspace))
      expect(reopened.modules.some((module) => module.source.musicrat_generated)).toBe(false)
    }
  })

  it('shows actual generated modules and runtime routes only in read-only implementation view', () => {
    const workspace = connect(fixture(), uiSlider, parameter)
    const normal = projectUnifiedGraph(workspace)
    expect(normal.nodes.some((node) => node.data.generated)).toBe(false)
    const debug = projectUnifiedGraph(workspace, {
      showImplementation: true, catalog: workspace.modules.map((module) => module.descriptor),
    })
    expect(debug.nodes.some((node) => node.data.generated && node.data.title === 'ControlMapper_1')).toBe(true)
    expect(debug.edges.some((edge) => edge.data?.route.kind === 'generated')).toBe(true)
    expect(debug.warning).toContain('without installed descriptors')
    const generatedEdge = debug.edges.find((edge) => edge.data?.route.kind === 'generated')!
    expect(() => connectGraph(workspace, debug, {
      source: generatedEdge.source, sourceHandle: generatedEdge.sourceHandle!,
      target: generatedEdge.target, targetHandle: generatedEdge.targetHandle!,
    })).toThrow('read-only')
    expect(disconnectGraph(workspace, [generatedEdge.data!.route])).toEqual(workspace)
  })

  it('reports compiler failures explicitly in implementation view without inventing a valid runtime graph', () => {
    const workspace = connect(fixture(), hardware, parameter)
    workspace.source.musicrat_control!.devices[0].adapter_module_id = undefined
    const debug = projectUnifiedGraph(workspace, { showImplementation: true })
    expect(debug.warning).toContain('requires a source owner with an adapter')
    expect(debug.nodes.some((node) => node.data.generated)).toBe(false)
  })

  it('removes modules with their semantic bindings without mutating the original project', () => {
    const workspace = connect(fixture(), uiSlider, parameter)
    const original = structuredClone(workspace)
    const updated = removeGraphModules(workspace, ['Instrument_1'])
    expect(workspace).toEqual(original)
    expect(updated.modules.some((module) => module.id === 'Instrument_1')).toBe(false)
    expect(updated.source.musicrat_control!.bindings).toEqual([])
    expect(updated.source.musicrat_control!.surfaces[0].widgets[0].parameter).toBeUndefined()
  })
})

function addSemanticModule(workspace: DesignerWorkspace): DesignerModule {
  const module: DesignerModule = { id: 'Player', position: { x: 400, y: 0 },
    source: { name: 'Player', module_class: 'Player' },
    descriptor: { module_class: 'Player', binary: '',
      synced_inputs: ['CommRaT::Messages::DeckControlEventBlock'],
      outputs: ['CommRaT::Messages::LevelMeterBlock'],
      descriptor_metadata: {
        musicrat_ports: { schema_version: 1, ports: [
          { id: 'actions', display_name: 'Actions', direction: 'synced_input',
            port_index: 0, domain: 'control', required: false, max_connections: 1 },
          { id: 'levels', display_name: 'Levels', direction: 'output',
            port_index: 0, domain: 'telemetry', required: false, max_connections: 1 },
        ] },
        musicrat_actions: { schema_version: 1, actions: [
          { id: 'play', display_name: 'Play', group: 'Transport', input_port_id: 'actions',
            kind: 'trigger', event_type: 0, minimum: 0, maximum: 1, default_value: 1 },
        ] },
        musicrat_observables: { schema_version: 1, observables: [
          { id: 'peak', display_name: 'Peak', group: 'Levels', output_port_id: 'levels',
            selector: 'peak', kind: 'continuous', unit: '', minimum: 0, maximum: 1, channel_selectable: true },
        ] },
      },
    },
  }
  workspace.modules.push(module)
  return module
}

describe('unified actions and telemetry', () => {
  it('connects playback time to text through the shared observable target catalog', () => {
    const workspace = fixture()
    const player = addSemanticModule(workspace)
    player.descriptor.outputs![0] = 'CommRaT::Messages::PlaybackStatusBlock'
    player.descriptor.descriptor_metadata!.musicrat_observables!.observables = [{
      id: 'elapsed', display_name: 'Elapsed', group: 'Timeline', output_port_id: 'levels',
      selector: 'elapsed_time', kind: 'text', unit: '', minimum: 0, maximum: 1,
      channel_selectable: false,
    }]
    workspace.source.musicrat_control = addSurfaceWidget(workspace.source.musicrat_control!,
      'display-screen', uiComponentDefinition('text')!).control
    const linked = connect(workspace,
      (port) => port.socket.kind === 'observable',
      (port, node) => node.data.widget_id === 'text-1' && port.socket.kind === 'widget-feedback')
    expect(linked.source.musicrat_control!.observation_bindings![0].target.property).toBe('text')
    expect(compileLaunchApplication(linked).modules.some((module) =>
      module.module_class === 'MusicRaTPlaybackStatusUiAdapter')).toBe(true)
    expect(() => connect(workspace, (port) => port.socket.kind === 'observable',
      (port, node) => node.data.widget_id === 'meter-1'
        && port.socket.kind === 'widget-feedback')).toThrow('incompatible')
  })

  it('rejects control connections that would compete with existing telemetry feedback', () => {
    const workspace = fixture()
    addSemanticModule(workspace)
    const linked = connect(workspace, (port) => port.socket.kind === 'observable'
      && port.socket.reference.channel === 0, sliderFeedback)
    expect(() => connect(linked, uiSlider, parameter)).toThrow('Disconnect the widget feedback')
  })

  it('routes UI buttons to semantic actions through a generated action mapper', () => {
    const workspace = fixture()
    addSemanticModule(workspace)
    expect(projectUnifiedGraph(workspace).nodes.find((node) => node.id === graphModuleId('Player'))!
      .data.inputs.some((port) => port.socket.kind === 'physical')).toBe(false)
    expect(projectUnifiedGraph(workspace, { showAdvancedPorts: true }).nodes
      .find((node) => node.id === graphModuleId('Player'))!.data.inputs
      .some((port) => port.socket.kind === 'physical')).toBe(true)
    const linked = connect(workspace, (port, node) => node.data.widget_id === 'button-1'
      && port.socket.kind === 'control', (port) => port.socket.kind === 'action')
    expect(linked.source.musicrat_control!.action_bindings![0].target).toEqual({ module_id: 'Player', action_id: 'play' })
    expect(compileLaunchApplication(linked).modules.some((module) => module.musicrat_generated === 'action_mapper')).toBe(true)
    expect(() => connect(workspace, hardware, (port) => port.socket.kind === 'action')).toThrow('incompatible')
    expect(actionTargets(workspace.modules)).toHaveLength(1)
  })

  it('routes channel-specific telemetry to meters and generates adapters, and disconnects cleanly', () => {
    const workspace = fixture()
    addSemanticModule(workspace)
    let linked = connect(workspace, (port) => port.socket.kind === 'observable' && port.socket.reference.channel === 1,
      (port, node) => node.data.widget_id === 'meter-1' && port.socket.kind === 'widget-feedback')
    expect(linked.source.musicrat_control!.observation_bindings![0].source.channel).toBe(1)
    expect(compileLaunchApplication(linked).modules.some((module) =>
      module.musicrat_generated === 'observation_adapter')).toBe(true)
    const route = projectUnifiedGraph(linked).edges.find((edge) => edge.data?.route.kind === 'observation')!.data!.route
    linked = disconnectGraph(linked, [route])
    expect(linked.source.musicrat_control!.observation_bindings).toEqual([])
    expect(linked.source.musicrat_control!.surfaces[0].widgets.find((widget) => widget.kind === 'meter')!.observable).toBeUndefined()
  })
})
