import { Check, Radio, X } from 'lucide-react'
import { useRef, useState } from 'react'
import {
  endpointFromKey,
  endpointOptions,
  parameterTargetFromKey,
  parameterTargetOptions,
} from './binding-editor-model'
import {
  confirmLearnProposal,
  createLearnObservation,
  proposeLearnedBinding,
  type ControlLearnProposal,
} from './control-learn-model'
import type { DesignerModule, MusicRaTControlProject } from './model'

interface ControlLearnProps {
  control?: MusicRaTControlProject
  modules: DesignerModule[]
  onChange: (control: MusicRaTControlProject) => void
  onSelectBinding: (bindingId: string) => void
}

export function ControlLearn({
  control,
  modules,
  onChange,
  onSelectBinding,
}: ControlLearnProps) {
  const sources = control ? endpointOptions(control) : []
  const targets = parameterTargetOptions(modules)
  const [learning, setLearning] = useState(false)
  const [sourceKey, setSourceKey] = useState('')
  const [targetKey, setTargetKey] = useState('')
  const [value, setValue] = useState(0.5)
  const [proposal, setProposal] = useState<ControlLearnProposal | null>(null)
  const sequence = useRef(0)
  const selectedSource = sourceKey || sources[0]?.key || ''
  const selectedTarget = targetKey || targets[0]?.key || ''

  const stop = () => {
    setLearning(false)
    setProposal(null)
  }

  const observe = () => {
    if (!control) return
    const source = endpointFromKey(selectedSource)
    const target = parameterTargetFromKey(selectedTarget)
    if (!source || !target) return
    sequence.current += 1
    const observation = createLearnObservation(
      control,
      source,
      value,
      sequence.current,
    )
    setProposal(observation
      ? proposeLearnedBinding(control, modules, target, observation) ?? null
      : null)
  }

  const confirm = () => {
    if (!control || !proposal) return
    onChange(confirmLearnProposal(control, proposal))
    onSelectBinding(proposal.binding.id)
    stop()
  }

  const available = Boolean(control && sources.length > 0 && targets.length > 0)

  return <section className={`control-learn${learning ? ' is-learning' : ''}`}>
    <div className="section-heading">
      <h3>Control learn</h3>
      {learning
        ? <button className="mini-icon-button" type="button" title="Cancel learn"
            aria-label="Cancel learn" onClick={stop}><X size={15} /></button>
        : <button className="learn-button" type="button" disabled={!available}
            onClick={() => setLearning(true)}><Radio size={14} /> Learn</button>}
    </div>
    {learning && <div className="learn-form">
      <label><span>Target</span><select aria-label="Learn target" value={selectedTarget}
        onChange={(event) => { setTargetKey(event.target.value); setProposal(null) }}>
        {targets.map((target) => <option key={target.key} value={target.key}>{target.label}</option>)}
      </select></label>
      <label><span>Simulated input</span><select aria-label="Simulated endpoint"
        value={selectedSource}
        onChange={(event) => { setSourceKey(event.target.value); setProposal(null) }}>
        {sources.map((source) => <option key={source.key} value={source.key}>{source.label}</option>)}
      </select></label>
      <label><span>Value</span><input type="number" step="any" aria-label="Simulated value"
        value={value} onChange={(event) => setValue(Number(event.target.value))} /></label>
      <button className="observe-button" type="button" onClick={observe}>
        <Radio size={14} /> Observe movement
      </button>
      {proposal && <div className="learn-proposal" role="status">
        <span>{proposal.source_label}</span>
        <strong>{proposal.binding.mode}</strong>
        <span>{proposal.target_label}</span>
        <small>sequence {proposal.observation.sequence} · value {proposal.observation.value}</small>
        <button className="primary-button" type="button" onClick={confirm}>
          <Check size={14} /> Confirm binding
        </button>
      </div>}
    </div>}
  </section>
}
