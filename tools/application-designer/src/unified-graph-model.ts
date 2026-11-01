import { MarkerType, type Edge, type Node } from '@xyflow/react'
import { nextBindingId, nextSemanticBindingId } from './binding-editor-model'
import { compileLaunchApplication } from './control-compiler'
import {
  actionTargets, observableTargets, parameterTargets, supportsParameterState,
  widgetAcceptsObservable,
  widgetAcceptsFeedback,
} from './graph-targets'
import { connectKeyboard, disconnectKeyboardEdges, projectKeyboardGraph } from './keyboard-graph-model'
import { modulePortDisplayName } from './module-node-model'
import { isImplementationModule } from './module-picker-model'
import {
  bindingModesForEndpoint, keyboardNoteTargets, parametersOf, payloadType, portsOf,
  validateConnection, validateControlProject,
  type DesignerPosition, type DesignerWorkspace,
  type EndpointReference, type ModuleDescriptor,
  type ObservableReference, type ParameterTarget,
} from './model'
import { observeSurfaceWidget, routeSurfaceWidget, routeSurfaceWidgetAction, updateSurfaceWidget } from './ui-designer-model'
import { uiComponentDefinition } from './ui-component-catalog'

type Socket =
  | { kind: 'physical'; module_id: string; port_id: string }
  | { kind: 'parameter'; module_id: string; parameter_id: number }
  | { kind: 'state'; module_id: string; parameter_id: number }
  | { kind: 'action'; module_id: string; action_id: string }
  | { kind: 'observable'; reference: ObservableReference }
  | { kind: 'control' | 'feedback'; reference: EndpointReference }
  | { kind: 'widget-feedback'; surface_id: string; widget_id: string }
  | { kind: 'keyboard'; keyboard_id: string }

export interface GraphPort {
  id: string
  label: string
  domain: string
  socket: Socket
}

export interface GraphNodeData extends Record<string, unknown> {
  kind: 'module' | 'ui' | 'hardware' | 'implementation'
  title: string
  subtitle: string
  inputs: GraphPort[]
  outputs: GraphPort[]
  module_id?: string
  surface_id?: string
  widget_id?: string
  generated?: boolean
  startup_parameters?: number
  params?: Record<string, unknown>
}

export type GraphNode = Node<GraphNodeData, 'unified'>
export type GraphRoute =
  | { kind: 'physical'; id: string }
  | { kind: 'keyboard'; id: string }
  | { kind: 'binding' | 'action' | 'observation' | 'feedback'; id: string }
  | { kind: 'widget-state'; surface_id: string; widget_id: string }
  | { kind: 'generated' }
export type GraphEdge = Edge<{ route: GraphRoute }>
export interface GraphProjection { nodes: GraphNode[]; edges: GraphEdge[]; warning?: string }

function key(...parts: (string | number)[]): string { return encodeURIComponent(JSON.stringify(parts)) }
export function graphModuleId(moduleId: string): string { return key('module', moduleId) }
function widgetId(surfaceId: string, id: string): string { return key('widget', surfaceId, id) }
function endpointId(reference: EndpointReference): string {
  return key('endpoint', reference.owner_id, reference.endpoint_id)
}
function socketId(socket: Socket): string {
  switch (socket.kind) {
    case 'physical': return key(socket.kind, socket.module_id, socket.port_id)
    case 'parameter':
    case 'state': return key(socket.kind, socket.module_id, socket.parameter_id)
    case 'action': return key(socket.kind, socket.module_id, socket.action_id)
    case 'observable': return key(socket.kind, socket.reference.module_id,
      socket.reference.observable_id, socket.reference.channel ?? -1)
    case 'control':
    case 'feedback': return key(socket.kind, socket.reference.owner_id, socket.reference.endpoint_id)
    case 'widget-feedback': return key(socket.kind, socket.surface_id, socket.widget_id)
    case 'keyboard': return key(socket.kind, socket.keyboard_id)
  }
}
function socketPort(socket: Socket, label: string, domain: string): GraphPort {
  return { id: socketId(socket), socket, label, domain }
}
function edge(id: string, source: GraphNode, output: GraphPort, target: GraphNode, input: GraphPort,
  route: GraphRoute, label?: string): GraphEdge {
  return { id, source: source.id, sourceHandle: output.id, target: target.id, targetHandle: input.id,
    data: { route }, label, className: `edge-${output.domain}${route.kind === 'generated' ? ' generated-edge' : ''}`,
    markerEnd: { type: MarkerType.ArrowClosed } }
}

export function projectUnifiedGraph(workspace: DesignerWorkspace, options: {
  showImplementation?: boolean; showAdvancedPorts?: boolean; catalog?: ModuleDescriptor[]
} = {}): GraphProjection {
  const nodes: GraphNode[] = []
  const edges: GraphEdge[] = []
  const control = workspace.source.musicrat_control
  const saved = workspace.source.musicrat_designer
  const parameters = parameterTargets(workspace.modules)
  const actions = actionTargets(workspace.modules)
  const observables = observableTargets(workspace.modules)
  const keyboard = projectKeyboardGraph(workspace)
  const node = (id: string, data: GraphNodeData, position: DesignerPosition): GraphNode => {
    const item: GraphNode = { id, type: 'unified', position: saved?.graph_positions?.[id] ?? position,
      deletable: data.kind === 'module', data }
    nodes.push(item)
    return item
  }
  for (const module of workspace.modules) {
    const implementation = isImplementationModule(module.descriptor)
    if (implementation && !options.showImplementation) continue
    const inputs: GraphPort[] = []
    const outputs: GraphPort[] = []
    for (const port of portsOf(module.descriptor)) {
      const connected = workspace.connections.some((route) =>
        route.source_module_id === module.id && route.source_port_id === port.id
        || route.target_module_id === module.id && route.target_port_id === port.id)
      const payload = payloadType(module.descriptor, port)
      const semantic = ['CommRaT::Messages::ParameterEventBlock', 'CommRaT::Messages::ParameterStateBlock',
        'CommRaT::Messages::DeckControlEventBlock'].includes(payload ?? '')
      if (!options.showAdvancedPorts && !options.showImplementation && semantic && !connected) continue
      const socket: Socket = { kind: 'physical', module_id: module.id, port_id: port.id }
      ;(port.direction === 'output' ? outputs : inputs).push(
        socketPort(socket, modulePortDisplayName(module.descriptor, port), port.domain))
    }
    for (const { parameter } of parameters.filter((target) => target.module.id === module.id)) {
      inputs.push(socketPort({ kind: 'parameter', module_id: module.id, parameter_id: parameter.id },
        parameter.display_name, 'parameter'))
      if (supportsParameterState(module)) outputs.push(socketPort({
        kind: 'state', module_id: module.id, parameter_id: parameter.id,
      }, `${parameter.display_name} state`, 'feedback'))
    }
    for (const { action } of actions.filter((target) => target.module.id === module.id)) {
      inputs.push(socketPort({ kind: 'action', module_id: module.id, action_id: action.id },
        action.display_name, 'control'))
    }
    for (const { observable } of observables.filter((target) => target.module.id === module.id)) {
      for (const channel of observable.channel_selectable ? [0, 1] : [undefined]) {
        outputs.push(socketPort({ kind: 'observable', reference: {
          module_id: module.id, observable_id: observable.id, channel,
        } }, `${observable.display_name}${channel === undefined ? '' : ` / Ch ${channel + 1}`}`, 'feedback'))
      }
    }
    node(graphModuleId(module.id), {
      kind: implementation ? 'implementation' : 'module', title: module.id,
      subtitle: module.descriptor.module_class.replace(/^MusicRaT/, ''), module_id: module.id,
      inputs, outputs, params: module.source.params ?? module.descriptor.params_defaults ?? {},
      startup_parameters: parametersOf(module.descriptor).length
        - parameters.filter((target) => target.module.id === module.id).length,
    }, module.position)
  }

  let widgetIndex = 0
  for (const surface of control?.surfaces ?? []) {
    for (const widget of surface.widgets) {
      const inputs: GraphPort[] = []
      const outputs: GraphPort[] = []
      const endpoint = surface.endpoints.find((candidate) => candidate.id === widget.endpoint_id)
      if (endpoint && bindingModesForEndpoint(endpoint).length > 0) {
        outputs.push(socketPort({ kind: 'control', reference: {
          owner_id: surface.id, endpoint_id: endpoint.id,
        } }, 'Control', 'control'))
      }
      const keyboardNode = keyboard.nodes.find((item) =>
        item.data.surface_id === surface.id && item.data.widget_id === widget.id)
      if (keyboardNode) outputs.push(socketPort({ kind: 'keyboard', keyboard_id: keyboardNode.id },
        'Note events', 'note'))
      if (surface.target === 'lvgl' && widgetAcceptsFeedback(widget)) {
        inputs.push(socketPort({ kind: 'widget-feedback', surface_id: surface.id, widget_id: widget.id },
          'Feedback', 'feedback'))
      }
      node(widgetId(surface.id, widget.id), { kind: 'ui', title: widget.display_name,
        subtitle: `${surface.display_name} / ${widget.kind}`, surface_id: surface.id,
        widget_id: widget.id, inputs, outputs },
      (keyboardNode && saved?.keyboard_positions?.[keyboardNode.id])
        ?? { x: -360, y: 100 + widgetIndex * 160 })
      widgetIndex++
    }
  }
  let endpointIndex = 0
  for (const owner of [...(control?.devices ?? []), ...(control?.surfaces ?? [])]) {
    for (const endpoint of owner.endpoints) {
      if ('widgets' in owner && owner.widgets.some((widget) => widget.endpoint_id === endpoint.id)) continue
      const reference = { owner_id: owner.id, endpoint_id: endpoint.id }
      const inputs = endpoint.direction !== 'input' ? [
        socketPort({ kind: 'feedback', reference }, 'Feedback', 'feedback'),
      ] : []
      const outputs = bindingModesForEndpoint(endpoint).length > 0 ? [
        socketPort({ kind: 'control', reference }, endpoint.display_name, 'control'),
      ] : []
      node(endpointId(reference), { kind: 'kind' in owner && owner.kind === 'hardware' ? 'hardware' : 'ui',
        title: endpoint.display_name, subtitle: `${owner.display_name} / ${endpoint.domain}`, inputs, outputs },
      saved?.binding_positions?.[`owner:${encodeURIComponent(owner.id)}`]
        ?? { x: -700, y: 100 + endpointIndex * 160 })
      endpointIndex++
    }
  }
  const find = (socket: Socket, direction: 'inputs' | 'outputs') => {
    const id = socketId(socket)
    for (const item of nodes) {
      const port = item.data[direction].find((candidate) => candidate.id === id)
      if (port) return { node: item, port }
    }
  }
  const cable = (id: string, source: Socket, target: Socket, route: GraphRoute, label?: string) => {
    const from = find(source, 'outputs')
    const to = find(target, 'inputs')
    if (from && to) edges.push(edge(id, from.node, from.port, to.node, to.port, route, label))
  }
  for (const route of workspace.connections) cable(key('physical', route.id),
    { kind: 'physical', module_id: route.source_module_id, port_id: route.source_port_id },
    { kind: 'physical', module_id: route.target_module_id, port_id: route.target_port_id },
    { kind: 'physical', id: route.id })
  for (const route of keyboard.edges) cable(key('keyboard', route.id),
    { kind: 'keyboard', keyboard_id: route.source },
    { kind: 'physical', module_id: route.target, port_id: route.targetHandle! },
    { kind: 'keyboard', id: route.id })
  for (const binding of control?.bindings ?? []) {
    const target: ParameterTarget = binding.target
    cable(key('binding', binding.id), { kind: 'control', reference: binding.source },
      { kind: 'parameter', ...target }, { kind: 'binding', id: binding.id }, binding.mode)
    if (binding.feedback) {
      const surface = control?.surfaces.find((item) => item.id === binding.feedback!.owner_id)
      const widget = surface?.widgets.find((item) => item.endpoint_id === binding.feedback!.endpoint_id)
      cable(key('feedback', binding.id), { kind: 'state', ...target },
        widget && surface ? { kind: 'widget-feedback', surface_id: surface.id, widget_id: widget.id }
          : { kind: 'feedback', reference: binding.feedback },
        { kind: 'feedback', id: binding.id }, 'state')
    }
  }
  for (const binding of control?.action_bindings ?? []) cable(key('action', binding.id),
    { kind: 'control', reference: binding.source }, { kind: 'action', ...binding.target },
    { kind: 'action', id: binding.id }, binding.quantization)
  for (const binding of control?.observation_bindings ?? []) cable(key('observation', binding.id),
    { kind: 'observable', reference: { ...binding.source,
      channel: observables.find((item) => item.module.id === binding.source.module_id
        && item.observable.id === binding.source.observable_id)?.observable.channel_selectable
        ? binding.source.channel ?? 0 : undefined } },
    { kind: 'widget-feedback', surface_id: binding.target.surface_id, widget_id: binding.target.widget_id },
    { kind: 'observation', id: binding.id })
  for (const surface of control?.surfaces ?? []) for (const widget of surface.widgets) {
    if (!widget.parameter || widget.parameter_feedback === false || edges.some((item) => item.target === widgetId(surface.id, widget.id)
      && item.data?.route.kind === 'feedback')) continue
    cable(key('widget-state', surface.id, widget.id), { kind: 'state', ...widget.parameter },
      { kind: 'widget-feedback', surface_id: surface.id, widget_id: widget.id },
      { kind: 'widget-state', surface_id: surface.id, widget_id: widget.id }, 'state')
  }
  if (!options.showImplementation) return { nodes, edges }
  try {
    const application = compileLaunchApplication(workspace)
    const descriptors = new Map((options.catalog ?? []).map((descriptor) => [descriptor.module_class, descriptor]))
    const addresses = new Map<string, { node: GraphNode; port: GraphPort }>()
    const compiledNodes = new Map<string, GraphNode>()
    const missing: string[] = []
    for (const module of application.modules) {
      let item = nodes.find((candidate) => candidate.data.module_id === module.name)
      if (!item) {
        const descriptor = descriptors.get(module.module_class)
        if (!descriptor) missing.push(module.module_class)
        const inputs: GraphPort[] = []
        const outputs: GraphPort[] = []
        for (const [direction, routes] of Object.entries({
          output: module.outputs, input: module.inputs, synced_input: module.synced_inputs, remote: module.remotes,
        })) {
          for (const [index] of (routes ?? []).entries()) {
            const port = descriptor && portsOf(descriptor).find((candidate) =>
              candidate.direction === direction && candidate.port_index === index)
            const value = socketPort({ kind: 'physical', module_id: module.name,
              port_id: port?.id ?? `${direction}:${index}` }, port?.display_name ?? `${direction} ${index}`, port?.domain ?? 'control')
            ;(direction === 'output' ? outputs : inputs).push(value)
          }
        }
        item = node(graphModuleId(module.name), {
          kind: 'implementation', title: module.name, subtitle: `${module.module_class} (generated)`,
          module_id: module.name, generated: true, inputs, outputs, params: module.params,
        }, { x: Math.max(0, ...workspace.modules.map((candidate) => candidate.position.x)) + 450,
          y: nodes.filter((candidate) => candidate.data.generated).length * 230 })
      }
      compiledNodes.set(module.name, item)
      for (const [index, address] of (module.outputs ?? []).entries()) {
        const descriptor = workspace.modules.find((candidate) => candidate.id === module.name)?.descriptor
          ?? descriptors.get(module.module_class)
        const portId = descriptor && portsOf(descriptor).find((port) =>
          port.direction === 'output' && port.port_index === index)?.id
        const port = item.data.outputs.find((candidate) =>
          candidate.socket.kind === 'physical'
          && candidate.socket.port_id === (portId ?? `output:${index}`))
        if (port) addresses.set(key(address.system_id, address.instance_id), { node: item, port })
      }
    }
    for (const module of application.modules) {
      const target = compiledNodes.get(module.name)!
      for (const [direction, routes] of Object.entries({
        input: module.inputs, synced_input: module.synced_inputs, remote: module.remotes,
      })) for (const [index, route] of (routes ?? []).entries()) {
        if (!route) continue
        const source = addresses.get(key(route.source_system_id, route.source_instance_id))
        const descriptor = workspace.modules.find((candidate) => candidate.id === module.name)?.descriptor
          ?? descriptors.get(module.module_class)
        const portId = descriptor && portsOf(descriptor).find((port) =>
          port.direction === direction && port.port_index === index)?.id
        const input = target.data.inputs.find((candidate) => candidate.socket.kind === 'physical'
          && candidate.socket.port_id === (portId ?? `${direction}:${index}`))
        if (source && input && (source.node.data.generated || target.data.generated)) {
          edges.push(edge(key('generated', module.name, direction, index),
            source.node, source.port, target, input, { kind: 'generated' }, 'generated'))
        }
      }
    }
    return { nodes, edges, warning: missing.length
      ? `Generated modules without installed descriptors (positional ports shown): ${[...new Set(missing)].join(', ')}.` : undefined }
  } catch (error) {
    return { nodes, edges, warning: `Implementation preview unavailable: ${
      error instanceof Error ? error.message : 'Application cannot be compiled.'}` }
  }
}

export interface GraphConnection { source: string | null; sourceHandle: string | null; target: string | null; targetHandle: string | null }

export function connectGraph(workspace: DesignerWorkspace, projection: GraphProjection,
  connection: GraphConnection): { workspace: DesignerWorkspace; bindingId?: string } {
  const sourceNode = projection.nodes.find((node) => node.id === connection.source)
  const targetNode = projection.nodes.find((node) => node.id === connection.target)
  if (sourceNode?.data.generated || targetNode?.data.generated) {
    throw new Error('Generated implementation routes are read-only. Edit the user-facing cable instead.')
  }
  const source = sourceNode?.data.outputs.find((port) => port.id === connection.sourceHandle)?.socket
  const target = targetNode?.data.inputs.find((port) => port.id === connection.targetHandle)?.socket
  if (!source || !target) throw new Error('Choose an output and a compatible input.')
  if (source.kind === 'physical' && target.kind === 'physical') {
    const candidate = { id: key(source.module_id, source.port_id, target.module_id, target.port_id),
      source_module_id: source.module_id, source_port_id: source.port_id,
      target_module_id: target.module_id, target_port_id: target.port_id }
    const problem = validateConnection(workspace.modules, candidate, workspace.connections)[0]
    if (problem) throw new Error(problem.message)
    if (projection.edges.some((route) => route.target === connection.target
      && route.targetHandle === connection.targetHandle)) throw new Error('The target port already has a connection.')
    return { workspace: { ...workspace, connections: [...workspace.connections, candidate] } }
  }
  const control = workspace.source.musicrat_control
  if (!control) throw new Error('Add a UI or hardware control element first.')
  let updated = control
  let bindingId: string | undefined
  if (source.kind === 'keyboard' && target.kind === 'physical') {
    updated = connectKeyboard(workspace, source.keyboard_id, { module_id: target.module_id, port_id: target.port_id })
  } else if (source.kind === 'control' && (target.kind === 'parameter' || target.kind === 'action')) {
    const owner = [...control.devices, ...control.surfaces].find((item) => item.id === source.reference.owner_id)!
    const endpoint = owner.endpoints.find((item) => item.id === source.reference.endpoint_id)!
    const widget = 'widgets' in owner ? owner.widgets.find((item) => item.endpoint_id === endpoint.id) : undefined
    if (widget?.observation_binding_id || widget?.parameter && (target.kind !== 'parameter'
      || widget.parameter.module_id !== target.module_id || widget.parameter.parameter_id !== target.parameter_id)) {
      throw new Error('Disconnect the widget feedback cable before choosing a different control target.')
    }
    if (control.bindings.some((binding) => endpointId(binding.source) === endpointId(source.reference)
      && target.kind === 'parameter' && binding.target.module_id === target.module_id
      && binding.target.parameter_id === target.parameter_id)
      || (control.action_bindings ?? []).some((binding) => target.kind === 'action'
        && endpointId(binding.source) === endpointId(source.reference)
        && binding.target.module_id === target.module_id && binding.target.action_id === target.action_id)) {
      throw new Error('This control is already linked to that target.')
    }
    if (target.kind === 'parameter') {
      if (!parameterTargets(workspace.modules).some((item) => item.module.id === target.module_id
        && item.parameter.id === target.parameter_id)) throw new Error('Parameter is not controllable.')
      if (widget) {
        if (widget.binding_id || widget.action_binding_id) throw new Error('Disconnect the widget control cable before choosing another target.')
        const definition = uiComponentDefinition(widget.kind)!
        const module = workspace.modules.find((item) => item.id === target.module_id)!
        updated = routeSurfaceWidget(control, owner.id, widget.id,
          { module_id: target.module_id, parameter_id: target.parameter_id },
          supportsParameterState(module) && (widget.route === 'bidirectional'
            || widget.parameter && widget.parameter_feedback !== false) ? 'bidirectional' : 'control', definition)
        bindingId = updated.surfaces.find((item) => item.id === owner.id)!.widgets.find((item) => item.id === widget.id)!.binding_id
      } else {
        bindingId = nextBindingId([...control.bindings, ...(control.action_bindings ?? [])])
        updated = { ...control, bindings: [...control.bindings, {
          id: bindingId, source: source.reference,
          target: { module_id: target.module_id, parameter_id: target.parameter_id },
          mode: bindingModesForEndpoint(endpoint)[0],
          pickup: 'immediate',
        }] }
      }
    } else {
      if (!actionTargets(workspace.modules, endpoint).some((item) =>
        item.module.id === target.module_id && item.action.id === target.action_id)) {
        throw new Error('Control domain is incompatible with this action.')
      }
      if (widget) {
        if (widget.binding_id || widget.action_binding_id) throw new Error('Disconnect the widget control cable before choosing another target.')
        updated = routeSurfaceWidgetAction(control, owner.id, widget.id,
          { module_id: target.module_id, action_id: target.action_id }, 'control', uiComponentDefinition(widget.kind)!)
      } else updated = { ...control, action_bindings: [...(control.action_bindings ?? []), {
        id: nextSemanticBindingId([...(control.action_bindings ?? []), ...control.bindings], 'action'),
        source: source.reference, target: { module_id: target.module_id, action_id: target.action_id }, quantization: 'immediate',
      }] }
    }
  } else if (source.kind === 'observable' && target.kind === 'widget-feedback') {
    const surface = control.surfaces.find((item) => item.id === target.surface_id)!
    const widget = surface.widgets.find((item) => item.id === target.widget_id)!
    const observable = observableTargets(workspace.modules).find((item) =>
      item.module.id === source.reference.module_id && item.observable.id === source.reference.observable_id)!.observable
    if (!widgetAcceptsObservable(widget, observable)) {
      throw new Error('This feedback value is incompatible with the widget property.')
    }
    if (widget.parameter || widget.observation_binding_id) throw new Error('Disconnect existing widget feedback before choosing another source.')
    updated = observeSurfaceWidget(control, surface.id, widget.id, source.reference)
  } else if (source.kind === 'state' && target.kind === 'widget-feedback') {
    const surface = control.surfaces.find((item) => item.id === target.surface_id)!
    const widget = surface.widgets.find((item) => item.id === target.widget_id)!
    if (widget.observation_binding_id || widget.parameter && (widget.parameter_feedback !== false
      || widget.parameter.module_id !== source.module_id || widget.parameter.parameter_id !== source.parameter_id)) {
      throw new Error('The widget already has a feedback source or controls a different parameter.')
    }
    if (widget.action_binding_id) throw new Error('Action widgets cannot display parameter state.')
    updated = updateSurfaceWidget(control, surface.id, widget.id, {
      parameter: { module_id: source.module_id, parameter_id: source.parameter_id },
      parameter_feedback: true, route: widget.binding_id ? 'bidirectional' : 'feedback',
    })
  } else if (source.kind === 'state' && target.kind === 'feedback') {
    const matching = control.bindings.filter((binding) => binding.target.module_id === source.module_id
      && binding.target.parameter_id === source.parameter_id)
    const sameEndpoint = matching.filter((binding) => endpointId(binding.source) === endpointId(target.reference))
    const sameOwner = matching.filter((binding) => binding.source.owner_id === target.reference.owner_id)
    const candidates = sameEndpoint.length ? sameEndpoint : sameOwner.length ? sameOwner : matching
    if (candidates.length !== 1) throw new Error('Hardware feedback needs one unambiguous control binding to this parameter. Choose its feedback endpoint in the binding inspector.')
    bindingId = candidates[0].id
    if (candidates[0].feedback) throw new Error('The binding already has feedback.')
    updated = { ...control, bindings: control.bindings.map((binding) => binding.id === bindingId
      ? { ...binding, feedback: target.reference } : binding) }
  } else throw new Error('Incompatible cable. Connect controls to parameters/actions, notes to note inputs, or state/telemetry to feedback elements.')
  const issues = validateControlProject(updated, workspace.modules)
  if (issues.length > 0) throw new Error(issues[0].message)
  return { workspace: { ...workspace, source: { ...workspace.source, musicrat_control: updated } }, bindingId }
}

export function disconnectGraph(workspace: DesignerWorkspace, routes: GraphRoute[]): DesignerWorkspace {
  if (routes.every((route) => route.kind === 'generated')) return workspace
  const ids = (kind: GraphRoute['kind']) => new Set(routes.flatMap((route) =>
    route.kind === kind && 'id' in route ? [route.id] : []))
  const control = workspace.source.musicrat_control
  let updated = control
  if (control) {
    const bindings = ids('binding')
    const actions = ids('action')
    const observations = ids('observation')
    const feedback = ids('feedback')
    updated = {
      ...control,
      bindings: control.bindings.filter((binding) => !bindings.has(binding.id))
        .map((binding) => feedback.has(binding.id) ? { ...binding, feedback: undefined } : binding),
      action_bindings: (control.action_bindings ?? []).filter((binding) => !actions.has(binding.id)),
      observation_bindings: (control.observation_bindings ?? []).filter((binding) => !observations.has(binding.id)),
      surfaces: control.surfaces.map((surface) => ({
        ...surface, widgets: surface.widgets.map((widget) => {
          const removeControl = bindings.has(widget.binding_id ?? '') || actions.has(widget.action_binding_id ?? '')
          const removeState = routes.some((route) => route.kind === 'widget-state'
            && route.surface_id === surface.id && route.widget_id === widget.id)
            || feedback.has(widget.binding_id ?? '')
          const removeObservation = observations.has(widget.observation_binding_id ?? '')
          return { ...widget,
            ...(removeControl ? { binding_id: undefined, action_binding_id: undefined,
              parameter: widget.parameter_feedback !== false && !removeState ? widget.parameter : undefined,
              route: widget.parameter && widget.parameter_feedback !== false && !removeState ? 'feedback' as const : 'control' as const,
              action: undefined } : {}),
            ...(removeState ? { parameter: widget.binding_id && !removeControl ? widget.parameter : undefined,
              parameter_feedback: false, route: 'control' as const } : {}),
            ...(removeObservation ? { observable: undefined, observation_binding_id: undefined } : {}),
          }
        }),
      })),
    }
    const keyboardIds = [...ids('keyboard')]
    if (keyboardIds.length > 0) updated = disconnectKeyboardEdges(
      { ...workspace, source: { ...workspace.source, musicrat_control: updated } }, keyboardIds)
  }
  return { ...workspace, connections: workspace.connections.filter((connection) => !ids('physical').has(connection.id)),
    source: { ...workspace.source, ...(updated ? { musicrat_control: updated } : {}) } }
}

export function moveGraphNodes(workspace: DesignerWorkspace, positions: Record<string, DesignerPosition>): DesignerWorkspace {
  return { ...workspace,
    modules: workspace.modules.map((module) => ({
      ...module, position: positions[graphModuleId(module.id)] ?? module.position,
    })),
    source: { ...workspace.source, musicrat_designer: {
      ...workspace.source.musicrat_designer, schema_version: 1,
      graph_positions: { ...workspace.source.musicrat_designer?.graph_positions, ...positions },
    } },
  }
}

export function removeGraphModules(workspace: DesignerWorkspace, moduleIds: string[]): DesignerWorkspace {
  if (moduleIds.length === 0) return workspace
  const removed = new Set(moduleIds)
  const projection = projectUnifiedGraph(workspace, { showImplementation: true })
  const routes = projection.edges.filter((item) => {
    const source = projection.nodes.find((node) => node.id === item.source)
    const target = projection.nodes.find((node) => node.id === item.target)
    return removed.has(source?.data.module_id ?? '') || removed.has(target?.data.module_id ?? '')
  }).flatMap((item) => item.data ? [item.data.route] : [])
  const updated = disconnectGraph(workspace, routes)
  const control = updated.source.musicrat_control && structuredClone(updated.source.musicrat_control)
  if (control) {
    control.surfaces = control.surfaces.map((surface) => ({ ...surface,
      widgets: surface.widgets.map((widget) => ({ ...widget,
        note_target_module_id: undefined,
        note_targets: keyboardNoteTargets(widget, workspace.modules).filter((target) => !removed.has(target.module_id)),
        ...(widget.parameter && removed.has(widget.parameter.module_id)
          ? { parameter: undefined, binding_id: undefined } : {}),
      })),
    }))
    control.devices = control.devices.map((device) => device.adapter_module_id && removed.has(device.adapter_module_id)
      ? { ...device, adapter_module_id: undefined } : device)
  }
  return { ...updated, source: { ...updated.source, ...(control ? { musicrat_control: control } : {}) },
    modules: updated.modules.filter((module) => !removed.has(module.id)),
    connections: updated.connections.filter((connection) =>
      !removed.has(connection.source_module_id) && !removed.has(connection.target_module_id)) }
}
