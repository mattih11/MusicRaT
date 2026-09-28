import { describe, expect, it } from 'vitest'
import { componentDefinition } from './component-catalog'
import {
  clampDeckElementPosition,
  createDefaultDeck,
  createDefaultDeckElement,
  deckDeviceId,
  deriveDeckControlProject,
  resizeDeck,
  snapDeckElementPosition,
} from './deck-editor-model'
import { demoApplication } from './demo'

describe('catalog-driven deck model', () => {
  it('places components using catalog dimensions rather than user wiring', () => {
    const deck = createDefaultDeck()
    const fader = createDefaultDeckElement(deck, componentDefinition('fader-60mm')!)

    expect(fader).toEqual({
      id: 'fader-1',
      component_id: 'fader-60mm',
      display_name: 'Fader',
      position: { x_mm: 10, y_mm: 10 },
    })
    expect(componentDefinition(fader.component_id)).toMatchObject({
      width_mm: 22,
      height_mm: 80,
      driver_class: 'absolute-analog',
    })
  })

  it('keeps dragged and rotated components inside the panel', () => {
    const deck = createDefaultDeck()
    const fader = componentDefinition('fader-60mm')!

    expect(clampDeckElementPosition(deck, fader, { x_mm: 400, y_mm: -20 })).toEqual({
      x_mm: 298,
      y_mm: 0,
    })
    expect(clampDeckElementPosition(deck, fader, {
      x_mm: -20,
      y_mm: 200,
      rotation_deg: 90,
    })).toEqual({
      x_mm: 29,
      y_mm: 89,
      rotation_deg: 90,
    })
  })

  it('snaps movement and reflows components when deck dimensions change', () => {
    const deck = createDefaultDeck()
    const knob = componentDefinition('knob-25mm')!
    const element = createDefaultDeckElement(deck, knob)
    element.position = { x_mm: 294, y_mm: 113 }
    deck.elements.push(element)

    expect(snapDeckElementPosition(deck, knob, { x_mm: 43, y_mm: 27 }, 10))
      .toEqual({ x_mm: 40, y_mm: 30 })
    expect(resizeDeck(deck, { width_mm: 200, height_mm: 100, thickness_mm: 4 }))
      .toMatchObject({
        panel: { width_mm: 200, height_mm: 100, thickness_mm: 4 },
        elements: [{ position: { x_mm: 175, y_mm: 75 } }],
      })
  })

  it('derives semantic endpoints and LVGL surfaces from placed components', () => {
    const control = structuredClone(demoApplication.musicrat_control!)
    const deck = createDefaultDeck()
    deck.elements.push(
      createDefaultDeckElement(deck, componentDefinition('motor-fader-100mm')!),
    )
    deck.elements.push(
      createDefaultDeckElement(deck, componentDefinition('touch-display-5in')!),
    )

    const derived = deriveDeckControlProject(control, deck)
    expect(derived.devices.find((device) => device.id === deckDeviceId)).toMatchObject({
      display_name: 'Deck controls',
      kind: 'hardware',
      endpoints: [{
        id: 'motor-fader-1',
        direction: 'bidirectional',
        domain: 'unipolar',
        capabilities: ['absolute', 'feedback', 'motorized', 'touch-sensitive'],
      }],
    })
    expect(derived.surfaces).toContainEqual({
      id: 'display-touch-display-1',
      display_name: '5-inch touch display',
      target: 'lvgl',
      endpoints: [],
      widgets: [],
    })
  })

  it('removes bindings when their component is removed', () => {
    const control = structuredClone(demoApplication.musicrat_control!)
    const deck = createDefaultDeck()
    const knob = createDefaultDeckElement(deck, componentDefinition('knob-25mm')!)
    deck.elements.push(knob)
    let derived = deriveDeckControlProject(control, deck)
    derived.bindings.push({
      id: 'knob-route',
      source: { owner_id: deckDeviceId, endpoint_id: knob.id },
      target: { module_id: 'Instrument_1', parameter_id: 1 },
      mode: 'absolute',
    })

    derived = deriveDeckControlProject(derived, { ...deck, elements: [] })
    expect(derived.bindings).toEqual([])
    expect(derived.devices.find((device) => device.id === deckDeviceId)?.endpoints).toEqual([])
  })
})
