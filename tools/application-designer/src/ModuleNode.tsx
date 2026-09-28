import { Handle, Position, type Node, type NodeProps } from '@xyflow/react'
import { Activity, AudioLines, CircleGauge, Music2, SlidersHorizontal } from 'lucide-react'
import { modulePortDisplayName, visibleModulePorts } from './module-node-model'
import { portsOf, type DesignerModule, type PortDomain } from './model'

export type ModuleNode = Node<DesignerModule, 'musicratModule'>

const domainIcons = {
  audio: AudioLines,
  note: Music2,
  parameter: SlidersHorizontal,
  control: CircleGauge,
  transport: Activity,
  telemetry: Activity,
  command: CircleGauge,
} satisfies Record<PortDomain, typeof Activity>

export function ModuleNodeView({ data, selected }: NodeProps<ModuleNode>) {
  const ports = visibleModulePorts(
    data.descriptor,
    portsOf(data.descriptor),
    data.showAdvancedPorts === true,
  )
  const inputs = ports.filter((port) => port.direction !== 'output')
  const outputs = ports.filter((port) => port.direction === 'output')
  const rows = Math.max(inputs.length, outputs.length, 1)

  return (
    <article className={`module-node${selected ? ' is-selected' : ''}`}>
      <header>
        <span className="module-kind">{data.descriptor.execution_mode ?? 'module'}</span>
        <strong>{data.id}</strong>
        <small>{data.descriptor.module_class.replace('MusicRaT', '')}</small>
      </header>
      <div className="port-grid" style={{ '--port-rows': rows } as React.CSSProperties}>
        <div className="port-column input-ports">
          {inputs.map((port) => {
            const Icon = domainIcons[port.domain]
            const label = modulePortDisplayName(data.descriptor, port)
            return (
              <div className={`port port-${port.domain}`} key={port.id}>
                <Handle type="target" position={Position.Left} id={port.id} title={`${label} (${port.domain})`} />
                <Icon size={12} aria-hidden="true" />
                <span>{label}</span>
              </div>
            )
          })}
        </div>
        <div className="port-column output-ports">
          {outputs.map((port) => {
            const Icon = domainIcons[port.domain]
            const label = modulePortDisplayName(data.descriptor, port)
            return (
              <div className={`port port-${port.domain}`} key={port.id}>
                <span>{label}</span>
                <Icon size={12} aria-hidden="true" />
                <Handle type="source" position={Position.Right} id={port.id} title={`${label} (${port.domain})`} />
              </div>
            )
          })}
        </div>
      </div>
    </article>
  )
}