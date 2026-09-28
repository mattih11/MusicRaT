import catalogData from './component-catalog.json'
import type {
  ControlEndpointCapability,
  ControlEndpointDirection,
  ControlValueDomain,
} from './model'

export type ComponentVisual =
  | 'knob'
  | 'fader'
  | 'motor-fader'
  | 'encoder'
  | 'button'
  | 'switch'
  | 'touch-display'
  | 'display'

export interface DeckComponentDefinition {
  id: string
  name: string
  kind: 'control' | 'display'
  visual: ComponentVisual
  width_mm: number
  height_mm: number
  driver_class: string
  endpoint?: {
    direction: ControlEndpointDirection
    domain: ControlValueDomain
    capabilities: ControlEndpointCapability[]
  }
  touch?: boolean
}

export const componentCatalog = catalogData as DeckComponentDefinition[]

export function componentDefinition(id: string): DeckComponentDefinition | undefined {
  return componentCatalog.find((component) => component.id === id)
}
