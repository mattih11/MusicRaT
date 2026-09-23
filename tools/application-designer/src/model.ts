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
  [key: string]: unknown
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
  capabilities?: string[]
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

export interface SurfaceWidget {
  id: string
  kind: 'knob' | 'slider' | 'fader' | 'button' | 'toggle' | 'choice' | 'xy' | 'keyboard' | 'transport' | 'text' | 'display' | 'meter'
  display_name: string
  endpoint_id?: string
  binding_id?: string
}

export interface ControlSurface {
  id: string
  display_name: string
  target: 'ratgui' | 'lvgl' | 'generic'
  endpoints: ControlEndpoint[]
  widgets: SurfaceWidget[]
}

export interface MusicRaTControlProject {
  schema_version: 1
  devices: ControlDevice[]
  bindings: ControlBinding[]
  surfaces: ControlSurface[]
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
const bindingModes = new Set<ControlBinding['mode']>([
  'absolute', 'relative', 'toggle', 'momentary', 'gate', 'trigger', 'choice',
])

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

  for (const surface of control.surfaces) {
    if (!['ratgui', 'lvgl', 'generic'].includes(surface.target)) {
      issues.push({ target: surface.id, message: 'Surface target is invalid.' })
    }
    for (const id of duplicateIds(surface.widgets)) {
      issues.push({ target: id || surface.id, message: `Widget IDs must be nonempty and unique within ${surface.id}.` })
    }
    for (const widget of surface.widgets) {
      if (widget.endpoint_id
          && !surface.endpoints.some((endpoint) => endpoint.id === widget.endpoint_id)) {
        issues.push({ target: widget.id, message: 'Widget endpoint does not exist on its surface.' })
      }
      if (widget.binding_id && !bindings.has(widget.binding_id)) {
        issues.push({ target: widget.id, message: 'Widget binding does not exist.' })
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

  return { ...workspace.source, app_name: workspace.appName, modules }
}

export function importApplication(
  application: ApplicationDocument,
  descriptors: ModuleDescriptor[],
): DesignerWorkspace {
  const byClass = new Map(descriptors.map((descriptor) => [descriptor.module_class, descriptor]))
  const editableModules = application.modules.filter(
    (source) => source.musicrat_generated !== 'control_mapper')
  const modules = editableModules.map((source, index) => {
    const descriptor = byClass.get(source.module_class)
    if (!descriptor) throw new Error(`Missing descriptor for ${source.module_class}.`)
    return {
      id: source.name,
      descriptor,
      source: structuredClone(source),
      position: { x: 80 + (index % 3) * 300, y: 80 + Math.floor(index / 3) * 220 },
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