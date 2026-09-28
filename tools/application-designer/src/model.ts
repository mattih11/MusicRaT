import { componentDefinition } from './component-catalog'

export type PortDirection = 'output' | 'input' | 'synced_input' | 'remote'
export type PortDomain =
  | 'audio'
  | 'note'
  | 'control'
  | 'parameter'
  | 'transport'
  | 'telemetry'
  | 'command'

export interface PhysicalPort {
  id: string
  display_name: string
  direction: PortDirection
  port_index: number
  domain: PortDomain
  required: boolean
  max_connections: number
}

export interface ParameterChoice {
  value: number
  label: string
}

export interface ParameterDescriptor {
  id: number
  name: string
  display_name: string
  group: string
  kind: 'continuous' | 'integer' | 'boolean' | 'choice' | 'text'
  unit: string
  minimum: number
  maximum: number
  step: number
  display_scale: 'linear' | 'logarithmic' | 'decibel'
  automatable: boolean
  read_only: boolean
  choices: ParameterChoice[]
}

export type SemanticValueKind = 'continuous' | 'boolean' | 'choice' | 'trigger' | 'text'

export interface ActionDescriptor {
  id: string
  display_name: string
  group: string
  input_port_id: string
  kind: SemanticValueKind
  event_type: number
  minimum: number
  maximum: number
  default_value: number
}

export interface ObservableDescriptor {
  id: string
  display_name: string
  group: string
  output_port_id: string
  selector: string
  kind: SemanticValueKind
  unit: string
  minimum: number
  maximum: number
  channel_selectable: boolean
}

export interface ModuleDescriptor {
  module_class: string
  binary: string
  outputs?: string[]
  inputs?: string[]
  synced_inputs?: string[]
  remotes?: string[]
  execution_mode?: string
  default_period_ms?: number
  params_defaults?: Record<string, unknown>
  descriptor_metadata?: {
    musicrat_ports?: {
      schema_version: number
      ports: PhysicalPort[]
    }
    musicrat_parameters?: {
      schema_version: number
      parameters: ParameterDescriptor[]
    }
    musicrat_actions?: {
      schema_version: number
      actions: ActionDescriptor[]
    }
    musicrat_observables?: {
      schema_version: number
      observables: ObservableDescriptor[]
    }
    [key: string]: unknown
  }
  [key: string]: unknown
}

export interface Address {
  system_id: number
  instance_id: number
}

export interface InputAddress {
  source_system_id: number
  source_instance_id: number
}

export interface ApplicationModule {
  name: string
  module_class: string
  outputs?: Address[]
  inputs?: InputAddress[]
  synced_inputs?: InputAddress[]
  remotes?: InputAddress[]
  params?: Record<string, unknown>
  [key: string]: unknown
}

export interface ApplicationDocument {
  app_name: string
  modules: ApplicationModule[]
  companions?: unknown[]
  musicrat_control?: MusicRaTControlProject
  musicrat_designer?: MusicRaTDesignerState
  [key: string]: unknown
}

export interface DesignerPosition {
  x: number
  y: number
}

export interface MusicRaTDesignerState {
  schema_version: 1
  module_positions?: Record<string, DesignerPosition>
  binding_positions?: Record<string, DesignerPosition>
}

export type ControlValueDomain =
  | 'unipolar'
  | 'bipolar'
  | 'relative'
  | 'boolean'
  | 'choice'
  | 'gate'
  | 'trigger'
  | 'note'
  | 'transport'
  | 'text'
  | 'telemetry'

export type ControlEndpointDirection = 'input' | 'output' | 'bidirectional'
export type ControlEndpointCapability =
  | 'absolute'
  | 'relative'
  | 'momentary'
  | 'latched'
  | 'feedback'
  | 'motorized'
  | 'touch-sensitive'
  | 'color'
  | 'text'

export interface ControlEndpoint {
  id: string
  display_name: string
  direction: ControlEndpointDirection
  domain: ControlValueDomain
  minimum?: number
  maximum?: number
  step?: number
  unit?: string
  choices?: ParameterChoice[]
  capabilities?: ControlEndpointCapability[]
}

export interface ControlDevice {
  id: string
  display_name: string
  kind: 'hardware' | 'gui' | 'virtual'
  adapter_module_id?: string
  required?: boolean
  endpoints: ControlEndpoint[]
}

export interface EndpointReference {
  owner_id: string
  endpoint_id: string
}

export interface ParameterTarget {
  module_id: string
  parameter_id: number
}

export interface ControlTransform {
  scale?: number
  offset?: number
  invert?: boolean
  dead_zone?: number
  curve?: 'linear' | 'logarithmic' | 'exponential'
  quantization?: number
  hysteresis?: number
}

export interface ControlBinding {
  id: string
  source: EndpointReference
  target: ParameterTarget
  mode: 'absolute' | 'relative' | 'toggle' | 'momentary' | 'gate' | 'trigger' | 'choice'
  pickup?: 'immediate' | 'match'
  pickup_tolerance?: number
  transform?: ControlTransform
  feedback?: EndpointReference
  priority?: number
}

export interface ActionTarget {
  module_id: string
  action_id: string
}

export interface ActionBinding {
  id: string
  source: EndpointReference
  target: ActionTarget
  quantization: 'immediate' | 'beat' | 'bar'
  ramp_frames?: number
}

export interface ObservableReference {
  module_id: string
  observable_id: string
  channel?: number
}

export interface ObservationBinding {
  id: string
  source: ObservableReference
  target: { surface_id: string; widget_id: string; property: 'value' | 'active' | 'text' }
}

export function bindingModesForEndpoint(endpoint: ControlEndpoint): ControlBinding['mode'][] {
  if (endpoint.direction === 'output') return []
  switch (endpoint.domain) {
    case 'unipolar':
    case 'bipolar':
      return ['absolute']
    case 'relative':
      return ['relative']
    case 'boolean': {
      const modes: ControlBinding['mode'][] = []
      if (!endpoint.capabilities?.includes('latched')) modes.push('momentary')
      if (!endpoint.capabilities?.includes('momentary')) modes.push('toggle')
      return modes.length > 0 ? modes : ['momentary', 'toggle']
    }
    case 'gate':
      return ['gate']
    case 'trigger':
      return ['trigger']
    case 'choice':
      return ['choice']
    default:
      return []
  }
}

export interface SurfaceWidget {
  id: string
  kind: 'knob' | 'slider' | 'fader' | 'button' | 'toggle' | 'choice' | 'xy' | 'keyboard' | 'transport' | 'text' | 'display' | 'meter'
  display_name: string
  layout: { x: number; y: number; width: number; height: number }
  route: 'control' | 'feedback' | 'bidirectional'
  parameter?: ParameterTarget
  action?: ActionTarget
  observable?: ObservableReference
  endpoint_id?: string
  binding_id?: string
  action_binding_id?: string
  observation_binding_id?: string
}

export interface ControlSurface {
  id: string
  display_name: string
  target: 'ratgui' | 'lvgl' | 'generic'
  endpoints: ControlEndpoint[]
  widgets: SurfaceWidget[]
}

export interface DeckElement {
  id: string
  component_id: string
  display_name: string
  position: { x_mm: number; y_mm: number; rotation_deg?: number }
}

export interface PhysicalDeck {
  schema_version: 1
  panel: { width_mm: number; height_mm: number; thickness_mm: number }
  elements: DeckElement[]
}

export interface MusicRaTControlProject {
  schema_version: 1
  devices: ControlDevice[]
  bindings: ControlBinding[]
  action_bindings?: ActionBinding[]
  observation_bindings?: ObservationBinding[]
  surfaces: ControlSurface[]
  deck?: PhysicalDeck
}

export interface DesignerModule {
  id: string
  descriptor: ModuleDescriptor
  source: ApplicationModule
  position: { x: number; y: number }
  [key: string]: unknown
}

export interface DesignerConnection {
  id: string
  source_module_id: string
  source_port_id: string
  target_module_id: string
  target_port_id: string
}

export interface DesignerWorkspace {
  appName: string
  modules: DesignerModule[]
  connections: DesignerConnection[]
  source: ApplicationDocument
}

export interface ValidationIssue {
  target: string
  message: string
}

function duplicateIds(items: { id: string }[]): string[] {
  const seen = new Set<string>()
  return items.flatMap((item) => {
    if (!item.id.trim() || seen.has(item.id)) return [item.id]
    seen.add(item.id)
    return []
  })
}

function finiteOptional(value: number | undefined): boolean {
  return value === undefined || Number.isFinite(value)
}

const endpointDirections = new Set<ControlEndpointDirection>(['input', 'output', 'bidirectional'])
const endpointDomains = new Set<ControlValueDomain>([
  'unipolar', 'bipolar', 'relative', 'boolean', 'choice', 'gate', 'trigger',
  'note', 'transport', 'text', 'telemetry',
])
const endpointCapabilities = new Set<ControlEndpointCapability>([
  'absolute', 'relative', 'momentary', 'latched', 'feedback', 'motorized',
  'touch-sensitive', 'color', 'text',
])
const bindingModes = new Set<ControlBinding['mode']>([
  'absolute', 'relative', 'toggle', 'momentary', 'gate', 'trigger', 'choice',
])

function validateDeck(control: MusicRaTControlProject): ValidationIssue[] {
  const deck = control.deck
  if (!deck) return []
  const issues: ValidationIssue[] = []
  if (deck.schema_version !== 1) {
    issues.push({ target: 'deck', message: 'Deck schema version is unsupported.' })
  }
  if (![deck.panel.width_mm, deck.panel.height_mm, deck.panel.thickness_mm]
      .every((value) => Number.isFinite(value) && value > 0)) {
    issues.push({ target: 'deck', message: 'Deck panel dimensions must be finite and positive.' })
  }
  for (const id of duplicateIds(deck.elements)) {
    issues.push({ target: id || 'deck', message: 'Deck element IDs must be nonempty and unique.' })
  }
  for (const element of deck.elements) {
    const position = element.position
    const component = componentDefinition(element.component_id)
    if (!component) issues.push({ target: element.id, message: 'Deck element references an unknown catalog component.' })
    if (![position.x_mm, position.y_mm, position.rotation_deg ?? 0].every(Number.isFinite)
        || position.x_mm < 0 || position.y_mm < 0
        || (component && (position.x_mm + component.width_mm > deck.panel.width_mm
          || position.y_mm + component.height_mm > deck.panel.height_mm))) {
      issues.push({ target: element.id, message: 'Deck component must fit within the panel.' })
    }
  }
  return issues
}

export function validateControlProject(
  control: MusicRaTControlProject | undefined,
  modules: DesignerModule[],
): ValidationIssue[] {
  if (!control) return []
  const issues: ValidationIssue[] = []
  if (control.schema_version !== 1) {
    issues.push({ target: 'musicrat_control', message: 'Unsupported control schema version.' })
  }

  const owners = [...control.devices, ...control.surfaces]
  issues.push(...validateDeck(control))
  for (const device of control.devices) {
    if (device.adapter_module_id
        && !modules.some((module) => module.id === device.adapter_module_id)) {
      issues.push({
        target: device.id,
        message: `Device adapter module ${device.adapter_module_id} does not exist.`,
      })
    }
  }
  for (const id of duplicateIds(owners)) {
    issues.push({ target: id || 'musicrat_control', message: 'Control owner IDs must be nonempty and unique.' })
  }
  for (const owner of owners) {
    for (const id of duplicateIds(owner.endpoints)) {
      issues.push({ target: id || owner.id, message: `Endpoint IDs must be nonempty and unique within ${owner.id}.` })
    }
    for (const endpoint of owner.endpoints) {
      if (!endpointDirections.has(endpoint.direction) || !endpointDomains.has(endpoint.domain)) {
        issues.push({ target: endpoint.id, message: 'Endpoint direction or domain is invalid.' })
      } else if (!finiteOptional(endpoint.minimum) || !finiteOptional(endpoint.maximum)
          || !finiteOptional(endpoint.step)) {
        issues.push({ target: endpoint.id, message: 'Endpoint ranges must contain finite numbers.' })
      } else if (endpoint.minimum !== undefined && endpoint.maximum !== undefined
          && endpoint.minimum > endpoint.maximum) {
        issues.push({ target: endpoint.id, message: 'Endpoint minimum cannot exceed its maximum.' })
      }
      const capabilities = endpoint.capabilities ?? []
      if (new Set(capabilities).size !== capabilities.length
          || capabilities.some((capability) => !endpointCapabilities.has(capability))) {
        issues.push({ target: endpoint.id, message: 'Endpoint capabilities must be known and unique.' })
      }
      if (capabilities.includes('motorized')
          && (endpoint.direction !== 'bidirectional' || !capabilities.includes('feedback'))) {
        issues.push({
          target: endpoint.id,
          message: 'Motorized endpoints must be bidirectional and feedback-capable.',
        })
      }
      if (capabilities.includes('feedback') && endpoint.direction === 'input') {
        issues.push({ target: endpoint.id, message: 'Feedback endpoints must accept output.' })
      }
      if (capabilities.includes('relative') && endpoint.domain !== 'relative') {
        issues.push({ target: endpoint.id, message: 'Relative capability requires the relative domain.' })
      }
    }
  }

  const endpointExists = (reference: EndpointReference) => {
    const owner = owners.find((item) => item.id === reference.owner_id)
    return owner?.endpoints.some((endpoint) => endpoint.id === reference.endpoint_id) ?? false
  }
  const bindings = new Map<string, ControlBinding>()
  for (const binding of control.bindings) {
    if (!binding.id.trim() || bindings.has(binding.id)) {
      issues.push({ target: binding.id || 'musicrat_control', message: 'Binding IDs must be nonempty and unique.' })
      continue
    }
    bindings.set(binding.id, binding)
    if (!endpointExists(binding.source)) {
      issues.push({ target: binding.id, message: 'Binding source endpoint does not exist.' })
    } else {
      const sourceOwner = owners.find((owner) => owner.id === binding.source.owner_id)!
      const sourceEndpoint = sourceOwner.endpoints.find((endpoint) =>
        endpoint.id === binding.source.endpoint_id)!
      if (!bindingModesForEndpoint(sourceEndpoint).includes(binding.mode)) {
        issues.push({
          target: binding.id,
          message: `Binding mode ${binding.mode} is incompatible with its source endpoint.`,
        })
      }
    }
    if (binding.feedback && !endpointExists(binding.feedback)) {
      issues.push({ target: binding.id, message: 'Binding feedback endpoint does not exist.' })
    }
    const targetModule = modules.find((module) => module.id === binding.target.module_id)
    const targetParameter = targetModule && parametersOf(targetModule.descriptor)
      .find((parameter) => parameter.id === binding.target.parameter_id)
    if (!targetParameter) {
      issues.push({ target: binding.id, message: 'Binding target parameter does not exist.' })
    } else if (!targetParameter.automatable || targetParameter.read_only) {
      issues.push({ target: binding.id, message: 'Binding target parameter is not controllable.' })
    }
    if (!bindingModes.has(binding.mode)) {
      issues.push({ target: binding.id, message: 'Binding mode is invalid.' })
    }
    if (binding.pickup !== undefined && !['immediate', 'match'].includes(binding.pickup)) {
      issues.push({ target: binding.id, message: 'Binding pickup policy is invalid.' })
    }
    if (!finiteOptional(binding.pickup_tolerance) || (binding.pickup_tolerance ?? 0) < 0) {
      issues.push({ target: binding.id, message: 'Binding pickup tolerance must be nonnegative.' })
    }
    const transform = binding.transform
    if (transform && (!finiteOptional(transform.scale) || !finiteOptional(transform.offset)
        || !finiteOptional(transform.dead_zone) || !finiteOptional(transform.quantization)
        || !finiteOptional(transform.hysteresis))) {
      issues.push({ target: binding.id, message: 'Binding transforms must contain finite numbers.' })
    }
  }

  const actionBindings = new Map<string, ActionBinding>()
  for (const binding of control.action_bindings ?? []) {
    if (!binding.id.trim() || actionBindings.has(binding.id) || bindings.has(binding.id)) {
      issues.push({ target: binding.id || 'musicrat_control', message: 'Parameter and action binding IDs must be nonempty and unique.' })
      continue
    }
    actionBindings.set(binding.id, binding)
    if (!endpointExists(binding.source)) {
      issues.push({ target: binding.id, message: 'Action binding source endpoint does not exist.' })
    }
    const module = modules.find((item) => item.id === binding.target.module_id)
    if (!module || !actionsOf(module.descriptor).some((action) => action.id === binding.target.action_id)) {
      issues.push({ target: binding.id, message: 'Action binding target does not exist.' })
    }
    if (!['immediate', 'beat', 'bar'].includes(binding.quantization)
        || !finiteOptional(binding.ramp_frames) || (binding.ramp_frames ?? 0) < 0) {
      issues.push({ target: binding.id, message: 'Action binding timing is invalid.' })
    }
  }

  const observationBindings = new Map<string, ObservationBinding>()
  for (const binding of control.observation_bindings ?? []) {
    if (!binding.id.trim() || observationBindings.has(binding.id)) {
      issues.push({ target: binding.id || 'musicrat_control', message: 'Observation binding IDs must be nonempty and unique.' })
      continue
    }
    observationBindings.set(binding.id, binding)
    const module = modules.find((item) => item.id === binding.source.module_id)
    const observable = module && observablesOf(module.descriptor)
      .find((item) => item.id === binding.source.observable_id)
    if (!observable) {
      issues.push({ target: binding.id, message: 'Observation binding source does not exist.' })
    } else if (binding.source.channel !== undefined
        && (!observable.channel_selectable || binding.source.channel < 0
          || !Number.isInteger(binding.source.channel))) {
      issues.push({ target: binding.id, message: 'Observation binding channel is invalid.' })
    }
    const surface = control.surfaces.find((item) => item.id === binding.target.surface_id)
    if (!surface?.widgets.some((widget) => widget.id === binding.target.widget_id)) {
      issues.push({ target: binding.id, message: 'Observation binding target widget does not exist.' })
    }
  }

  for (const surface of control.surfaces) {
    if (!['ratgui', 'lvgl', 'generic'].includes(surface.target)) {
      issues.push({ target: surface.id, message: 'Surface target is invalid.' })
    }
    for (const id of duplicateIds(surface.widgets)) {
      issues.push({ target: id || surface.id, message: `Widget IDs must be nonempty and unique within ${surface.id}.` })
    }
    for (const widget of surface.widgets) {
      const layout = widget.layout
      if (![layout.x, layout.y, layout.width, layout.height].every(Number.isFinite)
          || layout.x < 0 || layout.y < 0 || layout.width <= 0 || layout.height <= 0
          || layout.x + layout.width > 100 || layout.y + layout.height > 100) {
        issues.push({ target: widget.id, message: 'Widget layout must fit within its display.' })
      }
      if (widget.endpoint_id
          && !surface.endpoints.some((endpoint) => endpoint.id === widget.endpoint_id)) {
        issues.push({ target: widget.id, message: 'Widget endpoint does not exist on its surface.' })
      }
      if (widget.binding_id && !bindings.has(widget.binding_id)) {
        issues.push({ target: widget.id, message: 'Widget binding does not exist.' })
      }
      if (widget.action_binding_id && !actionBindings.has(widget.action_binding_id)) {
        issues.push({ target: widget.id, message: 'Widget action binding does not exist.' })
      }
      if (widget.observation_binding_id
          && !observationBindings.has(widget.observation_binding_id)) {
        issues.push({ target: widget.id, message: 'Widget observation binding does not exist.' })
      }
      if (widget.parameter) {
        const module = modules.find((item) => item.id === widget.parameter?.module_id)
        const parameter = module && parametersOf(module.descriptor)
          .find((item) => item.id === widget.parameter?.parameter_id)
        if (!parameter) {
          issues.push({ target: widget.id, message: 'Widget parameter does not exist.' })
        }
      }
      if (widget.route !== 'feedback' && widget.parameter
          && (!widget.endpoint_id || !widget.binding_id)) {
        issues.push({ target: widget.id, message: 'Controllable widgets require an endpoint and binding.' })
      }
    }
  }
  return issues
}

const lists: Record<PortDirection, keyof ModuleDescriptor> = {
  output: 'outputs',
  input: 'inputs',
  synced_input: 'synced_inputs',
  remote: 'remotes',
}

export function portsOf(descriptor: ModuleDescriptor): PhysicalPort[] {
  return descriptor.descriptor_metadata?.musicrat_ports?.ports ?? []
}

export function parametersOf(descriptor: ModuleDescriptor): ParameterDescriptor[] {
  return descriptor.descriptor_metadata?.musicrat_parameters?.parameters ?? []
}

export function actionsOf(descriptor: ModuleDescriptor): ActionDescriptor[] {
  return descriptor.descriptor_metadata?.musicrat_actions?.actions ?? []
}

export function observablesOf(descriptor: ModuleDescriptor): ObservableDescriptor[] {
  return descriptor.descriptor_metadata?.musicrat_observables?.observables ?? []
}

export function payloadType(descriptor: ModuleDescriptor, port: PhysicalPort): string | undefined {
  const values = descriptor[lists[port.direction]]
  return Array.isArray(values) ? values[port.port_index] : undefined
}

export function validateConnection(
  modules: DesignerModule[],
  connection: DesignerConnection,
  existing: DesignerConnection[] = [],
): ValidationIssue[] {
  const source = modules.find((module) => module.id === connection.source_module_id)
  const target = modules.find((module) => module.id === connection.target_module_id)
  if (!source || !target) {
    return [{ target: connection.id, message: 'Connection references a missing module.' }]
  }
  const sourcePort = portsOf(source.descriptor).find((port) => port.id === connection.source_port_id)
  const targetPort = portsOf(target.descriptor).find((port) => port.id === connection.target_port_id)
  if (!sourcePort || !targetPort) {
    return [{ target: connection.id, message: 'Connection references a missing stable port ID.' }]
  }
  if (sourcePort.direction !== 'output' || !['input', 'synced_input', 'remote'].includes(targetPort.direction)) {
    return [{ target: connection.id, message: 'Connections must run from an output to an input.' }]
  }
  if (sourcePort.domain !== targetPort.domain) {
    return [{ target: connection.id, message: `${sourcePort.domain} cannot connect to ${targetPort.domain}.` }]
  }
  if (payloadType(source.descriptor, sourcePort) !== payloadType(target.descriptor, targetPort)) {
    return [{ target: connection.id, message: 'CommRaT payload types do not match.' }]
  }
  const occupied = existing.some((edge) =>
    edge.id !== connection.id
    && edge.target_module_id === connection.target_module_id
    && edge.target_port_id === connection.target_port_id)
  return occupied
    ? [{ target: connection.id, message: 'The target port already has a connection.' }]
    : []
}

export function validateWorkspace(workspace: DesignerWorkspace): ValidationIssue[] {
  const issues = workspace.connections.flatMap((connection) =>
    validateConnection(workspace.modules, connection, workspace.connections))
  issues.push(...validateControlProject(workspace.source.musicrat_control, workspace.modules))
  for (const module of workspace.modules) {
    for (const port of portsOf(module.descriptor)) {
      if (port.required && ['input', 'synced_input', 'remote'].includes(port.direction)) {
        const connected = workspace.connections.some((connection) =>
          connection.target_module_id === module.id && connection.target_port_id === port.id)
        if (!connected) {
          issues.push({ target: module.id, message: `${port.display_name} requires a connection.` })
        }
      }
    }
  }
  return issues
}

function inputKey(direction: PortDirection): 'inputs' | 'synced_inputs' | 'remotes' {
  if (direction === 'synced_input') return 'synced_inputs'
  if (direction === 'remote') return 'remotes'
  return 'inputs'
}

export function compileApplication(
  workspace: DesignerWorkspace,
  options: { validate?: boolean } = {},
): ApplicationDocument {
  if (options.validate !== false) {
    const issues = validateWorkspace(workspace)
    if (issues.length > 0) throw new Error(issues[0].message)
  }

  const outputAddresses = new Map<string, Address>()
  let instanceId = 1
  for (const module of workspace.modules) {
    for (let index = 0; index < (module.descriptor.outputs?.length ?? 0); ++index) {
      outputAddresses.set(`${module.id}:${index}`, { system_id: 10, instance_id: instanceId++ })
    }
  }

  const modules = workspace.modules.map((module) => {
    const compiled: ApplicationModule = {
      ...module.source,
      name: module.id,
      module_class: module.descriptor.module_class,
      outputs: (module.descriptor.outputs ?? []).map((_, index) =>
        outputAddresses.get(`${module.id}:${index}`)!),
      params: module.source.params ?? module.descriptor.params_defaults ?? {},
    }
    delete compiled.inputs
    delete compiled.synced_inputs
    delete compiled.remotes

    for (const connection of workspace.connections.filter((edge) => edge.target_module_id === module.id)) {
      const source = workspace.modules.find((item) => item.id === connection.source_module_id)!
      const sourcePort = portsOf(source.descriptor).find((port) => port.id === connection.source_port_id)!
      const targetPort = portsOf(module.descriptor).find((port) => port.id === connection.target_port_id)!
      const address = outputAddresses.get(`${source.id}:${sourcePort.port_index}`)!
      const key = inputKey(targetPort.direction)
      const routes = (compiled[key] ?? []) as InputAddress[]
      routes[targetPort.port_index] = {
        source_system_id: address.system_id,
        source_instance_id: address.instance_id,
      }
      compiled[key] = routes
    }
    return compiled
  })

  return {
    ...workspace.source,
    app_name: workspace.appName,
    modules,
    musicrat_designer: {
      ...workspace.source.musicrat_designer,
      schema_version: 1,
      module_positions: Object.fromEntries(workspace.modules.map((module) => [
        module.id,
        module.position,
      ])),
    },
  }
}

export function importApplication(
  application: ApplicationDocument,
  descriptors: ModuleDescriptor[],
): DesignerWorkspace {
  const byClass = new Map(descriptors.map((descriptor) => [descriptor.module_class, descriptor]))
  const editableModules = application.modules.filter(
    (source) => source.musicrat_generated !== 'control_mapper'
      && source.musicrat_generated !== 'action_mapper'
      && source.musicrat_generated !== 'observation_adapter'
      && source.musicrat_generated !== 'widget_update_merger'
      && source.musicrat_generated !== 'lvgl_widget_sink'
      && source.musicrat_generated !== 'control_feedback_router')
  const modules = editableModules.map((source, index) => {
    const descriptor = byClass.get(source.module_class)
    if (!descriptor) throw new Error(`Missing descriptor for ${source.module_class}.`)
    return {
      id: source.name,
      descriptor,
      source: structuredClone(source),
      position: application.musicrat_designer?.module_positions?.[source.name]
        ?? { x: 80 + (index % 3) * 300, y: 80 + Math.floor(index / 3) * 220 },
    }
  })

  const producers = new Map<string, { module: DesignerModule; port: PhysicalPort }>()
  for (const module of modules) {
    const outputs = module.source.outputs ?? []
    for (const port of portsOf(module.descriptor).filter((item) => item.direction === 'output')) {
      const address = outputs[port.port_index]
      if (address) producers.set(`${address.system_id}:${address.instance_id}`, { module, port })
    }
  }

  const connections: DesignerConnection[] = []
  for (const module of modules) {
    for (const port of portsOf(module.descriptor).filter((item) => item.direction !== 'output')) {
      const route = module.source[inputKey(port.direction)]?.[port.port_index]
      if (!route) continue
      const producer = producers.get(`${route.source_system_id}:${route.source_instance_id}`)
      if (!producer) continue
      connections.push({
        id: `${producer.module.id}:${producer.port.id}->${module.id}:${port.id}`,
        source_module_id: producer.module.id,
        source_port_id: producer.port.id,
        target_module_id: module.id,
        target_port_id: port.id,
      })
    }
  }

  return {
    appName: application.app_name,
    modules,
    connections,
    source: structuredClone(application),
  }
}