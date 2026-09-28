import { Plus, Trash2 } from 'lucide-react'
import { useState } from 'react'
import {
  createDefaultBinding,
  endpointFromKey,
  endpointKey,
  endpointOptions,
  parameterTargetFromKey,
  parameterTargetKey,
  parameterTargetOptions,
} from './binding-editor-model'
import type {
  ControlBinding,
  DesignerModule,
  MusicRaTControlProject,
} from './model'

const modes: ControlBinding['mode'][] = [
  'absolute',
  'relative',
  'toggle',
  'momentary',
  'gate',
  'trigger',
  'choice',
]

interface BindingEditorProps {
  control?: MusicRaTControlProject
  modules: DesignerModule[]
  onChange: (control: MusicRaTControlProject) => void
  selectedBindingId?: string | null
  onSelectBinding?: (bindingId: string | null) => void
}

export function BindingEditor({
  control,
  modules,
  onChange,
  selectedBindingId,
  onSelectBinding,
}: BindingEditorProps) {
  const [selectedId, setSelectedId] = useState<string | null>(null)
  const sources = control ? endpointOptions(control) : []
  const feedbackTargets = control ? endpointOptions(control, 'feedback') : []
  const targets = parameterTargetOptions(modules)
  const activeId = selectedBindingId ?? selectedId
  const selectBinding = (bindingId: string | null) => {
    setSelectedId(bindingId)
    onSelectBinding?.(bindingId)
  }
  const selected = control?.bindings.find((binding) => binding.id === activeId)
    ?? control?.bindings[0]

  const update = (change: (binding: ControlBinding) => ControlBinding) => {
    if (!control || !selected) return
    onChange({
      ...control,
      bindings: control.bindings.map((binding) =>
        binding === selected ? change(binding) : binding),
    })
  }

  const addBinding = () => {
    if (!control) return
    const binding = createDefaultBinding(control, modules)
    if (!binding) return
    onChange({ ...control, bindings: [...control.bindings, binding] })
    selectBinding(binding.id)
  }

  const deleteBinding = () => {
    if (!control || !selected) return
    const index = control.bindings.indexOf(selected)
    const bindings = control.bindings.filter((binding) => binding !== selected)
    onChange({ ...control, bindings })
    selectBinding(bindings[Math.min(index, bindings.length - 1)]?.id ?? null)
  }

  const updateTransform = (
    name: keyof NonNullable<ControlBinding['transform']>,
    value: number | boolean | 'linear' | 'logarithmic' | 'exponential',
  ) => update((binding) => ({
    ...binding,
    transform: { ...binding.transform, [name]: value },
  }))

  return <section className="binding-editor">
    <div className="section-heading">
      <h3>Bindings</h3>
      <button
        className="mini-icon-button"
        type="button"
        title="Add binding"
        aria-label="Add binding"
        disabled={!control || sources.length === 0 || targets.length === 0}
        onClick={addBinding}
      ><Plus size={15} /></button>
    </div>

    {!control || sources.length === 0 || targets.length === 0
      ? <p className="empty-state">No semantic endpoints or controllable parameters.</p>
      : <>
        <div className="binding-list" role="list" aria-label="Control bindings">
          {control.bindings.map((binding) => <button
            type="button"
            role="listitem"
            className={`binding-list-item ${binding === selected ? 'is-selected' : ''}`}
            key={binding.id}
            onClick={() => selectBinding(binding.id)}
          ><strong>{binding.id}</strong><small>{binding.mode}</small></button>)}
        </div>
        {selected && <div className="binding-form">
          <label><span>ID</span><input
            aria-label="Binding ID"
            value={selected.id}
            onChange={(event) => {
              const id = event.target.value
              update((binding) => ({ ...binding, id }))
              selectBinding(id)
            }}
          /></label>
          <label><span>Source</span><select
            aria-label="Binding source"
            value={endpointKey(selected.source)}
            onChange={(event) => {
              const source = endpointFromKey(event.target.value)
              if (source) update((binding) => ({ ...binding, source }))
            }}
          >{sources.map((option) => <option key={option.key} value={option.key}>{option.label}</option>)}</select></label>
          <label><span>Target</span><select
            aria-label="Binding target"
            value={parameterTargetKey(selected.target.module_id, selected.target.parameter_id)}
            onChange={(event) => {
              const target = parameterTargetFromKey(event.target.value)
              if (target) update((binding) => ({ ...binding, target }))
            }}
          >{targets.map((option) => <option key={option.key} value={option.key}>{option.label}</option>)}</select></label>
          <label><span>Mode</span><select
            aria-label="Binding mode"
            value={selected.mode}
            onChange={(event) => update((binding) => ({
              ...binding,
              mode: event.target.value as ControlBinding['mode'],
            }))}
          >{modes.map((mode) => <option key={mode} value={mode}>{mode}</option>)}</select></label>
          <label><span>Pickup</span><select
            aria-label="Binding pickup"
            value={selected.pickup ?? 'immediate'}
            onChange={(event) => update((binding) => ({
              ...binding,
              pickup: event.target.value as NonNullable<ControlBinding['pickup']>,
            }))}
          ><option value="immediate">immediate</option><option value="match">match</option></select></label>
          <label><span>Tolerance</span><input type="number" min="0" step="0.001"
            aria-label="Pickup tolerance"
            value={selected.pickup_tolerance ?? 0.01}
            onChange={(event) => update((binding) => ({
              ...binding,
              pickup_tolerance: Number(event.target.value),
            }))}
          /></label>
          <label><span>Feedback</span><select
            aria-label="Feedback endpoint"
            value={selected.feedback ? endpointKey(selected.feedback) : ''}
            onChange={(event) => {
              const feedback = endpointFromKey(event.target.value)
              update((binding) => ({ ...binding, feedback }))
            }}
          ><option value="">none</option>{feedbackTargets.map((option) => <option key={option.key} value={option.key}>{option.label}</option>)}</select></label>

          <div className="binding-subheading">Transform</div>
          <label><span>Curve</span><select aria-label="Transform curve"
            value={selected.transform?.curve ?? 'linear'}
            onChange={(event) => updateTransform(
              'curve', event.target.value as 'linear' | 'logarithmic' | 'exponential')}
          ><option value="linear">linear</option><option value="logarithmic">logarithmic</option><option value="exponential">exponential</option></select></label>
          {([
            ['scale', 'Scale', 1],
            ['offset', 'Offset', 0],
            ['dead_zone', 'Dead zone', 0],
            ['quantization', 'Quantization', 0],
            ['hysteresis', 'Hysteresis', 0],
          ] as const).map(([name, label, fallback]) => <label key={name}><span>{label}</span><input
            type="number"
            step="any"
            aria-label={label}
            value={selected.transform?.[name] ?? fallback}
            onChange={(event) => updateTransform(name, Number(event.target.value))}
          /></label>)}
          <label><span>Invert</span><input type="checkbox" aria-label="Invert transform"
            checked={selected.transform?.invert ?? false}
            onChange={(event) => updateTransform('invert', event.target.checked)}
          /></label>
          <label><span>Priority</span><input type="number" step="1" aria-label="Binding priority"
            value={selected.priority ?? 0}
            onChange={(event) => update((binding) => ({
              ...binding,
              priority: Number(event.target.value),
            }))}
          /></label>
          <button className="danger-button" type="button" onClick={deleteBinding}>
            <Trash2 size={15} /> Delete binding
          </button>
        </div>}
      </>}
  </section>
}
