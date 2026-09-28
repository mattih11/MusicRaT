import {
  payloadType,
  type ModuleDescriptor,
  type PhysicalPort,
} from './model'

const parameterEvents = 'CommRaT::Messages::ParameterEventBlock'
const parameterState = 'CommRaT::Messages::ParameterStateBlock'

export function modulePortDisplayName(
  descriptor: ModuleDescriptor,
  port: PhysicalPort,
): string {
  const payload = payloadType(descriptor, port)
  if (payload === parameterEvents) return 'Automation Events'
  if (payload === parameterState) return 'Parameter State'
  return port.display_name
}

export function isAdvancedModulePort(
  descriptor: ModuleDescriptor,
  port: PhysicalPort,
): boolean {
  if (port.required) return false
  const payload = payloadType(descriptor, port)
  return payload === parameterEvents || payload === parameterState
}

export function visibleModulePorts(
  descriptor: ModuleDescriptor,
  ports: PhysicalPort[],
  showAdvancedPorts: boolean,
): PhysicalPort[] {
  return showAdvancedPorts
    ? ports
    : ports.filter((port) => !isAdvancedModulePort(descriptor, port))
}
