import { componentDefinition, type DeckComponentDefinition } from './component-catalog'
import type { DeckElement, MusicRaTControlProject, PhysicalDeck } from './model'

export const deckDeviceId = 'deck-hardware'

function nextId(existing: string[], prefix: string): string {
  let index = 1
  while (existing.includes(`${prefix}-${index}`)) index += 1
  return `${prefix}-${index}`
}

export function createDefaultDeck(): PhysicalDeck {
  return {
    schema_version: 1,
    panel: { width_mm: 320, height_mm: 140, thickness_mm: 3 },
    elements: [],
  }
}

export function createDefaultDeckElement(
  deck: PhysicalDeck,
  component: DeckComponentDefinition,
): DeckElement {
  const ids = deck.elements.map((element) => element.id)
  const id = nextId(ids, component.visual)
  return {
    id,
    component_id: component.id,
    display_name: component.name,
    position: {
      x_mm: 10 + (deck.elements.length % 6) * 45,
      y_mm: 10,
    },
  }
}

export function clampDeckElementPosition(
  deck: PhysicalDeck,
  component: DeckComponentDefinition,
  position: DeckElement['position'],
): DeckElement['position'] {
  const radians = (position.rotation_deg ?? 0) * Math.PI / 180
  const footprintWidth = Math.abs(component.width_mm * Math.cos(radians))
    + Math.abs(component.height_mm * Math.sin(radians))
  const footprintHeight = Math.abs(component.width_mm * Math.sin(radians))
    + Math.abs(component.height_mm * Math.cos(radians))
  const minimumX = (footprintWidth - component.width_mm) / 2
  const minimumY = (footprintHeight - component.height_mm) / 2
  const maximumX = deck.panel.width_mm - (footprintWidth + component.width_mm) / 2
  const maximumY = deck.panel.height_mm - (footprintHeight + component.height_mm) / 2

  return {
    ...position,
    x_mm: Math.min(Math.max(position.x_mm, minimumX), Math.max(minimumX, maximumX)),
    y_mm: Math.min(Math.max(position.y_mm, minimumY), Math.max(minimumY, maximumY)),
  }
}

export function snapDeckElementPosition(
  deck: PhysicalDeck,
  component: DeckComponentDefinition,
  position: DeckElement['position'],
  gridSize: number,
): DeckElement['position'] {
  const snapped = gridSize > 0 ? {
    ...position,
    x_mm: Math.round(position.x_mm / gridSize) * gridSize,
    y_mm: Math.round(position.y_mm / gridSize) * gridSize,
  } : position
  return clampDeckElementPosition(deck, component, snapped)
}

export function resizeDeck(
  deck: PhysicalDeck,
  panel: PhysicalDeck['panel'],
): PhysicalDeck {
  const resized = { ...deck, panel }
  return {
    ...resized,
    elements: resized.elements.map((element) => {
      const component = componentDefinition(element.component_id)
      return component
        ? { ...element, position: clampDeckElementPosition(resized, component, element.position) }
        : element
    }),
  }
}

export function deriveDeckControlProject(
  control: MusicRaTControlProject,
  deck: PhysicalDeck,
): MusicRaTControlProject {
  const controls = deck.elements.flatMap((element) => {
    const component = componentDefinition(element.component_id)
    return component?.endpoint ? [{ element, endpoint: component.endpoint }] : []
  })
  const displays = deck.elements.filter((element) =>
    componentDefinition(element.component_id)?.kind === 'display')
  const endpointIds = new Set(controls.map(({ element }) => element.id))
  const bindings = control.bindings.filter((binding) =>
    binding.source.owner_id !== deckDeviceId || endpointIds.has(binding.source.endpoint_id))
    .map((binding) => binding.feedback?.owner_id === deckDeviceId
      && !endpointIds.has(binding.feedback.endpoint_id)
      ? { ...binding, feedback: undefined }
      : binding)
  const actionBindings = (control.action_bindings ?? []).filter((binding) =>
    binding.source.owner_id !== deckDeviceId || endpointIds.has(binding.source.endpoint_id))
  const deckDevice = {
    id: deckDeviceId,
    display_name: 'Deck controls',
    kind: 'hardware' as const,
    required: true,
    endpoints: controls.map(({ element, endpoint }) => ({
      id: element.id,
      display_name: element.display_name,
      direction: endpoint.direction,
      domain: endpoint.domain,
      minimum: endpoint.domain === 'relative' ? -1 : 0,
      maximum: 1,
      step: 0.01,
      capabilities: endpoint.capabilities,
    })),
  }
  const derivedSurfaces = displays.map((element) => ({
    id: `display-${element.id}`,
    display_name: element.display_name,
    target: 'lvgl' as const,
    endpoints: [],
    widgets: [],
  }))
  return {
    ...control,
    deck,
    bindings,
    action_bindings: actionBindings,
    devices: [...control.devices.filter((device) => device.id !== deckDeviceId), deckDevice],
    surfaces: [
      ...control.surfaces.filter((surface) => !surface.id.startsWith('display-')),
      ...derivedSurfaces.map((surface) => {
        const existing = control.surfaces.find((candidate) => candidate.id === surface.id)
        return existing ? { ...surface, widgets: existing.widgets } : surface
      }),
    ],
  }
}
