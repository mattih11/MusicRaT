import { nextBindingId } from './binding-editor-model'
import {
  bindingModesForEndpoint,
  parametersOf,
  type ControlBinding,
  type ControlEndpoint,
  type ControlValueDomain,
  type DesignerModule,
  type EndpointReference,
  type MusicRaTControlProject,
  type ParameterTarget,
} from './model'

export interface ControlLearnObservation {
  provider_id: string
  sequence: number
  source: EndpointReference
  domain: ControlValueDomain
  value: number
  gesture: 'begin' | 'update' | 'end'
}

export interface ControlLearnProposal {
  observation: ControlLearnObservation
  source_label: string
  target_label: string
  binding: ControlBinding
}

export function isNewerLearnObservation(
  previous: ControlLearnObservation | undefined,
  candidate: ControlLearnObservation,
): boolean {
  return !previous || previous.provider_id !== candidate.provider_id
    || candidate.sequence > previous.sequence
}

export function confirmLearnProposal(
  control: MusicRaTControlProject,
  proposal: ControlLearnProposal,
): MusicRaTControlProject {
  return {
    ...control,
    bindings: [...control.bindings, proposal.binding],
  }
}

function endpointFor(
  control: MusicRaTControlProject,
  reference: EndpointReference,
): { endpoint: ControlEndpoint; ownerLabel: string } | undefined {
  const owner = [...control.devices, ...control.surfaces]
    .find((item) => item.id === reference.owner_id)
  const endpoint = owner?.endpoints.find((item) => item.id === reference.endpoint_id)
  return owner && endpoint ? { endpoint, ownerLabel: owner.display_name } : undefined
}

export function createLearnObservation(
  control: MusicRaTControlProject,
  source: EndpointReference,
  value: number,
  sequence: number,
  providerId = 'simulated',
): ControlLearnObservation | undefined {
  const found = endpointFor(control, source)
  if (!found || bindingModesForEndpoint(found.endpoint).length === 0
      || !Number.isFinite(value) || !Number.isSafeInteger(sequence) || sequence <= 0) {
    return undefined
  }
  return {
    provider_id: providerId,
    sequence,
    source,
    domain: found.endpoint.domain,
    value,
    gesture: 'update',
  }
}

export function proposeLearnedBinding(
  control: MusicRaTControlProject,
  modules: DesignerModule[],
  target: ParameterTarget,
  observation: ControlLearnObservation,
): ControlLearnProposal | undefined {
  const found = endpointFor(control, observation.source)
  const module = modules.find((item) => item.id === target.module_id)
  const parameter = module && parametersOf(module.descriptor).find((item) =>
    item.id === target.parameter_id && item.automatable && !item.read_only
      && item.kind !== 'text')
  if (!found || !parameter || found.endpoint.domain !== observation.domain) return undefined
  const mode = bindingModesForEndpoint(found.endpoint)[0]
  if (!mode) return undefined
  const feedback = found.endpoint.direction === 'bidirectional'
      && found.endpoint.capabilities?.includes('feedback')
    ? observation.source
    : undefined
  const binding: ControlBinding = {
    id: nextBindingId(control.bindings),
    source: observation.source,
    target,
    mode,
    pickup: mode === 'absolute' ? 'match' : 'immediate',
    transform: {
      scale: 1,
      offset: 0,
      invert: false,
      dead_zone: 0,
      curve: 'linear',
      quantization: 0,
      hysteresis: 0,
    },
    feedback,
  }
  return {
    observation,
    source_label: `${found.ownerLabel} / ${found.endpoint.display_name}`,
    target_label: `${module.id} / ${parameter.display_name}`,
    binding,
  }
}
