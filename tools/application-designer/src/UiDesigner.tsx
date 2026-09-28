import { Check, Trash2 } from 'lucide-react'
import { useRef, useState, type PointerEvent as ReactPointerEvent } from 'react'
import {
  actionTargetFromKey,
  actionTargetKey,
  actionTargetOptions,
  observableFromKey,
  observableKey,
  observableOptions,
  parameterTargetFromKey,
  parameterTargetKey,
} from './binding-editor-model'
import { componentDefinition } from './component-catalog'
import { actionsOf, parametersOf, type DesignerModule, type MusicRaTControlProject, type SurfaceWidget } from './model'
import { uiComponentCatalog, uiComponentDefinition } from './ui-component-catalog'
import {
  addSurfaceWidget,
  clampWidgetLayout,
  observeSurfaceWidget,
  removeSurfaceWidget,
  routeSurfaceWidget,
  routeSurfaceWidgetAction,
  updateSurfaceWidget,
} from './ui-designer-model'

interface UiDesignerProps {
  control?: MusicRaTControlProject
  modules: DesignerModule[]
  onChange: (control: MusicRaTControlProject) => void
}

export function UiDesigner({ control, modules, onChange }: UiDesignerProps) {
  const project = control ?? { schema_version: 1 as const, devices: [], bindings: [], surfaces: [] }
  const displaySurfaces = project.surfaces.filter((surface) => surface.target === 'lvgl')
  const [surfaceId, setSurfaceId] = useState<string | null>(null)
  const [selectedId, setSelectedId] = useState<string | null>(null)
  const [snapToGrid, setSnapToGrid] = useState(true)
  const [gridSize, setGridSize] = useState(5)
  const surface = displaySurfaces.find((item) => item.id === surfaceId) ?? displaySurfaces[0]
  const selected = surface?.widgets.find((widget) => widget.id === selectedId) ?? surface?.widgets[0]
  const definition = selected && uiComponentDefinition(selected.kind)
  const drag = useRef<{
    pointerId: number
    widgetId: string
    startClientX: number
    startClientY: number
    startX: number
    startY: number
    displayWidth: number
    displayHeight: number
  } | null>(null)
  const parameterOptions = modules.flatMap((module) => parametersOf(module.descriptor).map((parameter) => ({
    key: parameterTargetKey(module.id, parameter.id),
    label: `${module.id} / ${parameter.display_name}`,
    target: { module_id: module.id, parameter_id: parameter.id },
    controllable: parameter.automatable && !parameter.read_only,
  })))
  const actionOptions = actionTargetOptions(modules).filter((option) => {
    const module = modules.find((item) => item.id === option.target.module_id)
    const action = module && actionsOf(module.descriptor)
      .find((item) => item.id === option.target.action_id)
    return action && definition?.domain
      && (action.kind === definition.domain
        || action.kind === 'trigger' && ['trigger', 'boolean'].includes(definition.domain)
        || action.kind === 'continuous' && ['unipolar', 'bipolar'].includes(definition.domain))
  })
  const feedbackOptions = observableOptions(modules)
  const displayElement = project.deck?.elements.find((element) =>
    surface?.id === `display-${element.id}`)
  const displayDefinition = displayElement && componentDefinition(displayElement.component_id)
  const aspectRatio = displayDefinition
    ? `${displayDefinition.width_mm} / ${displayDefinition.height_mm}`
    : '16 / 9'

  const addWidget = (kind: SurfaceWidget['kind']) => {
    if (!surface) return
    const component = uiComponentDefinition(kind)
    if (!component) return
    const result = addSurfaceWidget(project, surface.id, component)
    onChange(result.control)
    setSelectedId(result.widgetId)
  }
  const updateSelected = (change: Partial<SurfaceWidget>) => {
    if (!surface || !selected) return
    onChange(updateSurfaceWidget(project, surface.id, selected.id, change))
  }
  const routeParameter = (key: string, route = selected?.route) => {
    if (!surface || !selected || !definition || !route) return
    const target = parameterTargetFromKey(key)
    onChange(routeSurfaceWidget(project, surface.id, selected.id, target, route, definition))
  }
  const routeControlTarget = (key: string) => {
    if (!surface || !selected || !definition) return
    if (key.startsWith('parameter:')) {
      routeParameter(key.slice('parameter:'.length))
      return
    }
    const target = key.startsWith('action:')
      ? actionTargetFromKey(key.slice('action:'.length))
      : undefined
    onChange(routeSurfaceWidgetAction(
      project, surface.id, selected.id, target, selected.route, definition))
  }
  const routeObservation = (key: string) => {
    if (!surface || !selected) return
    onChange(observeSurfaceWidget(project, surface.id, selected.id,
      key ? observableFromKey(key) : undefined))
  }
  const changeRoute = (route: SurfaceWidget['route']) => {
    if (!surface || !selected || !definition) return
    onChange(selected.action
      ? routeSurfaceWidgetAction(project, surface.id, selected.id, selected.action, route, definition)
      : routeSurfaceWidget(project, surface.id, selected.id, selected.parameter, route, definition))
  }
  const deleteSelected = () => {
    if (!surface || !selected) return
    onChange(removeSurfaceWidget(project, surface.id, selected.id))
    setSelectedId(null)
  }
  const startDrag = (event: ReactPointerEvent<HTMLButtonElement>, widget: SurfaceWidget) => {
    if (event.button !== 0) return
    const bounds = event.currentTarget.parentElement?.getBoundingClientRect()
    if (!bounds) return
    event.currentTarget.setPointerCapture(event.pointerId)
    setSelectedId(widget.id)
    drag.current = {
      pointerId: event.pointerId,
      widgetId: widget.id,
      startClientX: event.clientX,
      startClientY: event.clientY,
      startX: widget.layout.x,
      startY: widget.layout.y,
      displayWidth: bounds.width,
      displayHeight: bounds.height,
    }
  }
  const moveDrag = (event: ReactPointerEvent<HTMLButtonElement>, widget: SurfaceWidget) => {
    const active = drag.current
    if (!surface || !active || active.pointerId !== event.pointerId || active.widgetId !== widget.id) return
    const layout = clampWidgetLayout({
      ...widget.layout,
      x: active.startX + (event.clientX - active.startClientX) * 100 / active.displayWidth,
      y: active.startY + (event.clientY - active.startClientY) * 100 / active.displayHeight,
    }, snapToGrid ? gridSize : 0.1)
    onChange(updateSurfaceWidget(project, surface.id, widget.id, { layout }))
  }
  const stopDrag = (event: ReactPointerEvent<HTMLButtonElement>) => {
    if (drag.current?.pointerId !== event.pointerId) return
    drag.current = null
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId)
    }
  }

  return <div className="ui-workspace">
    <aside className="ui-catalog" aria-label="UI components">
      <div className="panel-heading"><div><span className="eyebrow">Widget catalog</span>
        <h2>Interface</h2></div><span className="count">{uiComponentCatalog.length}</span></div>
      <div className="ui-surface-picker">
        <label><span>Display</span><select aria-label="UI display" value={surface?.id ?? ''}
          onChange={(event) => { setSurfaceId(event.target.value); setSelectedId(null) }}>
          {displaySurfaces.length === 0 && <option value="">No displays</option>}
          {displaySurfaces.map((item) => <option key={item.id} value={item.id}>{item.display_name}</option>)}
        </select></label>
      </div>
      <div className="ui-catalog-list">
        {uiComponentCatalog.map((component) => <button type="button" key={component.kind}
          className="ui-catalog-item" disabled={!surface} onClick={() => addWidget(component.kind)}>
          <UiWidgetGlyph kind={component.kind} />
          <span><strong>{component.name}</strong><small>{component.default_route}</small></span>
        </button>)}
      </div>
    </aside>

    <section className="ui-stage" aria-label="UI design">
      <div className="ui-stage-toolbar">
        <div><strong>{surface?.display_name ?? 'Display layout'}</strong>
          <span>{displayDefinition ? `${displayDefinition.width_mm} × ${displayDefinition.height_mm} mm` : 'Add a display in Hardware'}</span></div>
        <div className="deck-toolbar-fields">
          <label className="snap-toggle"><input type="checkbox" checked={snapToGrid}
            onChange={(event) => setSnapToGrid(event.target.checked)} /> Snap</label>
          <label>Grid <input type="number" min="1" max="25" aria-label="UI grid size" value={gridSize}
            onChange={(event) => setGridSize(Math.max(1, Number(event.target.value) || 1))} /> %</label>
        </div>
      </div>
      {surface ? <div className={`ui-display${snapToGrid ? ' has-grid' : ''}`} style={{
        aspectRatio,
        backgroundSize: `${gridSize}% ${gridSize}%`,
      }}>
        {surface.widgets.map((widget) => <button type="button" key={widget.id}
          className={`ui-widget kind-${widget.kind}${widget === selected ? ' is-selected' : ''}`}
          style={{ left: `${widget.layout.x}%`, top: `${widget.layout.y}%`,
            width: `${widget.layout.width}%`, height: `${widget.layout.height}%` }}
          onClick={() => setSelectedId(widget.id)} onPointerDown={(event) => startDrag(event, widget)}
          onPointerMove={(event) => moveDrag(event, widget)} onPointerUp={stopDrag}
          onPointerCancel={stopDrag} title={`${widget.display_name} — drag to move`}>
          <UiWidgetGlyph kind={widget.kind} /><span>{widget.display_name}</span>
        </button>)}
        {surface.widgets.length === 0 && <div className="hardware-empty">Choose a widget to place it.</div>}
      </div> : <div className="ui-no-display">Place a display in the Hardware tab first.</div>}
    </section>

    <aside className="ui-inspector">
      <div className="panel-heading"><div><span className="eyebrow">UI inspector</span>
        <h2>{selected?.display_name ?? 'Display'}</h2></div></div>
      {!surface || !selected || !definition ? <p className="empty-state">Select or add a widget.</p> : <div className="hardware-form">
        <label><span>Name</span><input aria-label="Widget name" value={selected.display_name}
          onChange={(event) => updateSelected({ display_name: event.target.value })} /></label>
        <div className="ui-geometry-grid">
          {(['x', 'y', 'width', 'height'] as const).map((key) => <label key={key}><span>{key}</span>
            <input type="number" min="0" max="100" aria-label={`Widget ${key}`} value={selected.layout[key]}
              onChange={(event) => updateSelected({ layout: clampWidgetLayout({
                ...selected.layout, [key]: Number(event.target.value),
              }, snapToGrid ? gridSize : 0.1) })} /></label>)}
        </div>
        {definition.domain && <label><span>Route</span><select aria-label="Widget route" value={selected.route}
          onChange={(event) => changeRoute(event.target.value as SurfaceWidget['route'])}>
          <option value="control">Control</option>
          <option value="feedback">Feedback</option>
          <option value="bidirectional">Control + feedback</option>
        </select></label>}
        {selected.route !== 'feedback' && definition.domain && <label><span>Control target</span>
          <select aria-label="Widget control target"
            value={selected.parameter
              ? `parameter:${parameterTargetKey(selected.parameter.module_id, selected.parameter.parameter_id)}`
              : selected.action ? `action:${actionTargetKey(selected.action.module_id, selected.action.action_id)}` : ''}
            onChange={(event) => routeControlTarget(event.target.value)}>
            <option value="">Not routed</option>
            <optgroup label="Parameters">
              {parameterOptions.filter((option) => option.controllable).map((option) =>
                <option key={option.key} value={`parameter:${option.key}`}>{option.label}</option>)}
            </optgroup>
            <optgroup label="Actions">
              {actionOptions.map((option) => <option key={option.key}
                value={`action:${option.key}`}>{option.label}</option>)}
            </optgroup>
          </select></label>}
        {selected.route !== 'control' && <label><span>Feedback source</span>
          <select aria-label="Widget feedback source"
            value={selected.observable
              ? observableKey(selected.observable.module_id, selected.observable.observable_id,
                selected.observable.channel) : ''}
            onChange={(event) => routeObservation(event.target.value)}>
            <option value="">Not routed</option>
            {feedbackOptions.flatMap((option) => option.channel_selectable
              ? [0, 1].map((channel) => <option key={`${option.key}:${channel}`}
                  value={observableKey(option.reference.module_id,
                    option.reference.observable_id, channel)}>{option.label} / Ch {channel + 1}</option>)
              : [<option key={option.key} value={option.key}>{option.label}</option>])}
          </select></label>}
        {(selected.parameter || selected.action) && <div className="automatic-feedback">
          <Check size={14} /> Control binding created</div>}
        {selected.observable && <div className="automatic-feedback">
          <Check size={14} /> Feedback observation connected</div>}
        <button className="danger-button" type="button" onClick={deleteSelected}>
          <Trash2 size={15} /> Delete widget
        </button>
      </div>}
    </aside>
  </div>
}

function UiWidgetGlyph({ kind }: { kind: SurfaceWidget['kind'] }) {
  return <span className={`ui-widget-glyph glyph-${kind}`} aria-hidden="true" />
}
