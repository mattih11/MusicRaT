import { Check, Trash2 } from 'lucide-react'
import { useRef, useState, type PointerEvent as ReactPointerEvent } from 'react'
import { componentCatalog, componentDefinition } from './component-catalog'
import {
  clampDeckElementPosition,
  deckDeviceId,
  deriveDeckControlProject,
  createDefaultDeck,
  createDefaultDeckElement,
  resizeDeck,
  snapDeckElementPosition,
} from './deck-editor-model'
import {
  actionTargetFromKey,
  actionTargetKey,
  actionTargetOptions,
  nextBindingId,
  nextSemanticBindingId,
  parameterTargetFromKey,
  parameterTargetKey,
  parameterTargetOptions,
} from './binding-editor-model'
import { actionsOf, bindingModesForEndpoint, type DesignerModule, type MusicRaTControlProject } from './model'

interface DeckEditorProps {
  control?: MusicRaTControlProject
  modules: DesignerModule[]
  onChange: (control: MusicRaTControlProject) => void
}

export function DeckEditor({ control, modules, onChange }: DeckEditorProps) {
  const [selectedId, setSelectedId] = useState<string | null>(null)
  const [snapToGrid, setSnapToGrid] = useState(true)
  const [gridSize, setGridSize] = useState(5)
  const drag = useRef<{
    pointerId: number
    elementId: string
    startClientX: number
    startClientY: number
    startX: number
    startY: number
    panelWidth: number
    panelHeight: number
  } | null>(null)
  const project = control ?? { schema_version: 1 as const, devices: [], bindings: [], surfaces: [] }
  const deck = project.deck ?? createDefaultDeck()
  const selected = deck.elements.find((element) => element.id === selectedId) ?? deck.elements[0]
  const selectedComponent = selected && componentDefinition(selected.component_id)
  const targets = parameterTargetOptions(modules)
  const actionTargets = actionTargetOptions(modules).filter((option) => {
    const module = modules.find((item) => item.id === option.target.module_id)
    const action = module && actionsOf(module.descriptor)
      .find((item) => item.id === option.target.action_id)
    const domain = selectedComponent?.endpoint?.domain
    return action && domain && (action.kind === domain
      || action.kind === 'trigger' && ['trigger', 'boolean'].includes(domain)
      || action.kind === 'continuous' && ['unipolar', 'bipolar'].includes(domain))
  })
  const binding = selected && project.bindings.find((candidate) =>
    candidate.source.owner_id === deckDeviceId && candidate.source.endpoint_id === selected.id)
  const actionBinding = selected && (project.action_bindings ?? []).find((candidate) =>
    candidate.source.owner_id === deckDeviceId && candidate.source.endpoint_id === selected.id)

  const commitDeck = (nextDeck: typeof deck) => onChange(deriveDeckControlProject(project, nextDeck))
  const updatePanel = (key: keyof typeof deck.panel, value: number) => {
    if (!Number.isFinite(value) || value <= 0) return
    commitDeck(resizeDeck(deck, { ...deck.panel, [key]: value }))
  }
  const updateElement = (elementId: string, change: Partial<(typeof deck.elements)[number]>) => {
    commitDeck({
      ...deck,
      elements: deck.elements.map((element) =>
        element.id === elementId ? { ...element, ...change } : element),
    })
  }
  const addComponent = (componentId: string) => {
    const component = componentDefinition(componentId)
    if (!component) return
    const element = createDefaultDeckElement(deck, component)
    commitDeck({ ...deck, elements: [...deck.elements, element] })
    setSelectedId(element.id)
  }
  const updateSelected = (change: Partial<typeof selected>) => {
    if (!selected) return
    updateElement(selected.id, change)
  }
  const startDrag = (event: ReactPointerEvent<HTMLButtonElement>, element: (typeof deck.elements)[number]) => {
    if (event.button !== 0) return
    const panelBounds = event.currentTarget.parentElement?.getBoundingClientRect()
    if (!panelBounds) return
    event.currentTarget.setPointerCapture(event.pointerId)
    setSelectedId(element.id)
    drag.current = {
      pointerId: event.pointerId,
      elementId: element.id,
      startClientX: event.clientX,
      startClientY: event.clientY,
      startX: element.position.x_mm,
      startY: element.position.y_mm,
      panelWidth: panelBounds.width,
      panelHeight: panelBounds.height,
    }
  }
  const moveDrag = (
    event: ReactPointerEvent<HTMLButtonElement>,
    element: (typeof deck.elements)[number],
  ) => {
    const active = drag.current
    if (!active || active.pointerId !== event.pointerId || active.elementId !== element.id) return
    const component = componentDefinition(element.component_id)
    if (!component) return
    const requested = {
      ...element.position,
      x_mm: active.startX
        + (event.clientX - active.startClientX) * deck.panel.width_mm / active.panelWidth,
      y_mm: active.startY
        + (event.clientY - active.startClientY) * deck.panel.height_mm / active.panelHeight,
    }
    const position = snapToGrid
      ? snapDeckElementPosition(deck, component, requested, gridSize)
      : clampDeckElementPosition(deck, component, requested)
    updateElement(element.id, {
      position: {
        ...position,
        x_mm: Math.round(position.x_mm * 10) / 10,
        y_mm: Math.round(position.y_mm * 10) / 10,
      },
    })
  }
  const stopDrag = (event: ReactPointerEvent<HTMLButtonElement>) => {
    if (drag.current?.pointerId !== event.pointerId) return
    drag.current = null
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId)
    }
  }
  const removeSelected = () => {
    if (!selected) return
    const elements = deck.elements.filter((element) => element.id !== selected.id)
    commitDeck({ ...deck, elements })
    setSelectedId(elements[0]?.id ?? null)
  }
  const routeSelected = (key: string) => {
    if (!selected || !selectedComponent?.endpoint) return
    const parameterKey = key.startsWith('parameter:') ? key.slice('parameter:'.length) : ''
    const actionKey = key.startsWith('action:') ? key.slice('action:'.length) : ''
    const target = parameterTargetFromKey(parameterKey)
    const actionTarget = actionTargetFromKey(actionKey)
    const withoutCurrent = project.bindings.filter((candidate) => candidate !== binding)
    const withoutCurrentAction = (project.action_bindings ?? [])
      .filter((candidate) => candidate !== actionBinding)
    if (actionTarget) {
      onChange({
        ...deriveDeckControlProject(project, deck),
        bindings: withoutCurrent,
        action_bindings: [...withoutCurrentAction, {
          id: actionBinding?.id ?? nextSemanticBindingId(withoutCurrentAction, 'action'),
          source: { owner_id: deckDeviceId, endpoint_id: selected.id },
          target: actionTarget,
          quantization: 'immediate',
        }],
      })
      return
    }
    if (!target) {
      onChange({ ...deriveDeckControlProject(project, deck), bindings: withoutCurrent,
        action_bindings: withoutCurrentAction })
      return
    }
    const endpoint = {
      id: selected.id,
      display_name: selected.display_name,
      ...selectedComponent.endpoint,
    }
    const feedback = endpoint.direction === 'bidirectional'
      && endpoint.capabilities.includes('feedback')
      ? { owner_id: deckDeviceId, endpoint_id: selected.id }
      : undefined
    const nextBinding = {
      id: binding?.id ?? nextBindingId(withoutCurrent),
      source: { owner_id: deckDeviceId, endpoint_id: selected.id },
      target,
      mode: bindingModesForEndpoint(endpoint)[0]!,
      pickup: endpoint.domain === 'unipolar' ? 'match' as const : 'immediate' as const,
      feedback,
    }
    onChange({
      ...deriveDeckControlProject(project, deck),
      bindings: [...withoutCurrent, nextBinding],
      action_bindings: withoutCurrentAction,
    })
  }

  return <div className="hardware-workspace">
    <aside className="hardware-catalog" aria-label="Hardware components">
      <div className="panel-heading"><div><span className="eyebrow">Component catalog</span>
        <h2>Hardware</h2></div><span className="count">{componentCatalog.length}</span></div>
      <div className="hardware-catalog-list">
        {componentCatalog.map((component) => <button type="button" key={component.id}
          className="hardware-catalog-item" onClick={() => addComponent(component.id)}>
          <ComponentGlyph visual={component.visual} />
          <span><strong>{component.name}</strong>
            <small>{component.width_mm} × {component.height_mm} mm</small></span>
        </button>)}
      </div>
    </aside>

    <section className="hardware-stage" aria-label="Hardware design">
      <div className="hardware-stage-toolbar">
        <strong>Deck layout</strong>
        <div className="deck-toolbar-fields">
          <label>W <input type="number" min="1" aria-label="Deck width" value={deck.panel.width_mm}
            onChange={(event) => updatePanel('width_mm', Number(event.target.value))} /></label>
          <label>H <input type="number" min="1" aria-label="Deck height" value={deck.panel.height_mm}
            onChange={(event) => updatePanel('height_mm', Number(event.target.value))} /></label>
          <label>T <input type="number" min="0.1" step="0.1" aria-label="Deck thickness" value={deck.panel.thickness_mm}
            onChange={(event) => updatePanel('thickness_mm', Number(event.target.value))} /></label>
          <label className="snap-toggle"><input type="checkbox" checked={snapToGrid}
            onChange={(event) => setSnapToGrid(event.target.checked)} /> Snap</label>
          <label>Grid <input type="number" min="1" max="50" aria-label="Hardware grid size" value={gridSize}
            onChange={(event) => setGridSize(Math.max(1, Number(event.target.value) || 1))} /> mm</label>
        </div>
      </div>
      <div className={`hardware-panel${snapToGrid ? ' has-grid' : ''}`} style={{
        aspectRatio: `${deck.panel.width_mm} / ${deck.panel.height_mm}`,
        backgroundSize: `${gridSize / deck.panel.width_mm * 100}% ${gridSize / deck.panel.height_mm * 100}%`,
      }}>
        {deck.elements.map((element) => {
          const component = componentDefinition(element.component_id)!
          return <button type="button" key={element.id}
            className={`hardware-element visual-${component.visual}${element === selected ? ' is-selected' : ''}`}
            style={{
              left: `${element.position.x_mm / deck.panel.width_mm * 100}%`,
              top: `${element.position.y_mm / deck.panel.height_mm * 100}%`,
              width: `${component.width_mm / deck.panel.width_mm * 100}%`,
              height: `${component.height_mm / deck.panel.height_mm * 100}%`,
              transform: `rotate(${element.position.rotation_deg ?? 0}deg)`,
            }} onClick={() => setSelectedId(element.id)}
            onPointerDown={(event) => startDrag(event, element)}
            onPointerMove={(event) => moveDrag(event, element)}
            onPointerUp={stopDrag} onPointerCancel={stopDrag}
            title={`${element.display_name} — drag to move`}>
            <ComponentGlyph visual={component.visual} />
            <span>{element.display_name}</span>
          </button>
        })}
        {deck.elements.length === 0 && <div className="hardware-empty">Choose a component to place it.</div>}
      </div>
    </section>

    <aside className="hardware-inspector">
      <div className="panel-heading"><div><span className="eyebrow">Hardware inspector</span>
        <h2>{selected?.display_name ?? 'Deck'}</h2></div></div>
      {!selected || !selectedComponent ? <p className="empty-state">Select or add a component.</p> : <div className="hardware-form">
        <label><span>Name</span><input aria-label="Component name" value={selected.display_name}
          onChange={(event) => updateSelected({ display_name: event.target.value })} /></label>
        <div className="component-readonly"><span>Component</span><strong>{selectedComponent.name}</strong>
          <small>{selectedComponent.width_mm} × {selectedComponent.height_mm} mm · automatic driver</small></div>
        <label><span>X (mm)</span><input type="number" aria-label="Component X" value={selected.position.x_mm}
          onChange={(event) => updateSelected({ position: { ...selected.position, x_mm: Number(event.target.value) } })} /></label>
        <label><span>Y (mm)</span><input type="number" aria-label="Component Y" value={selected.position.y_mm}
          onChange={(event) => updateSelected({ position: { ...selected.position, y_mm: Number(event.target.value) } })} /></label>
        <label><span>Rotation</span><input type="number" step="90" aria-label="Component rotation"
          value={selected.position.rotation_deg ?? 0}
          onChange={(event) => updateSelected({ position: { ...selected.position, rotation_deg: Number(event.target.value) } })} /></label>
        {selectedComponent.endpoint && <label><span>Controls</span><select aria-label="Component target"
          value={binding ? `parameter:${parameterTargetKey(binding.target.module_id, binding.target.parameter_id)}`
            : actionBinding ? `action:${actionTargetKey(actionBinding.target.module_id, actionBinding.target.action_id)}` : ''}
          onChange={(event) => routeSelected(event.target.value)}>
          <option value="">Not routed</option>
          <optgroup label="Parameters">
            {targets.map((target) => <option key={target.key}
              value={`parameter:${target.key}`}>{target.label}</option>)}
          </optgroup>
          <optgroup label="Actions">
            {actionTargets.map((target) => <option key={target.key}
              value={`action:${target.key}`}>{target.label}</option>)}
          </optgroup>
        </select></label>}
        {binding?.feedback && <div className="automatic-feedback"><Check size={14} /> Feedback routed automatically</div>}
        {selectedComponent.kind === 'display' && <div className="automatic-feedback"><Check size={14} /> LVGL surface created automatically</div>}
        <button className="danger-button" type="button" onClick={removeSelected}>
          <Trash2 size={15} /> Delete component
        </button>
      </div>}
    </aside>
  </div>
}

function ComponentGlyph({ visual }: { visual: string }) {
  return <span className={`component-glyph glyph-${visual}`} aria-hidden="true" />
}
