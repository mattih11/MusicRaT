import {
  Background,
  BackgroundVariant,
  Controls,
  MarkerType,
  MiniMap,
  ReactFlow,
  applyNodeChanges,
  type Connection,
  type NodeChange,
} from '@xyflow/react'
import { useEffect, useMemo, useState } from 'react'
import {
  bindingFromConnection,
  projectBindingCanvas,
  type BindingCanvasNode,
} from './binding-canvas-model'
import { BindingOwnerNodeView, BindingTargetNodeView } from './BindingNodes'
import { createEmptyControlProject } from './control-project-model'
import type {
  DesignerModule,
  DesignerPosition,
  MusicRaTControlProject,
} from './model'

const bindingNodeTypes = {
  bindingOwner: BindingOwnerNodeView,
  bindingTarget: BindingTargetNodeView,
}

interface BindingCanvasProps {
  control?: MusicRaTControlProject
  modules: DesignerModule[]
  positions?: Record<string, DesignerPosition>
  onControlChange: (control: MusicRaTControlProject) => void
  onPositionsChange: (positions: Record<string, DesignerPosition>) => void
  onSelectBinding: (bindingId: string | null) => void
}

export function BindingCanvas({
  control,
  modules,
  positions,
  onControlChange,
  onPositionsChange,
  onSelectBinding,
}: BindingCanvasProps) {
  const projection = useMemo(
    () => projectBindingCanvas(control, modules, positions),
    [control, modules, positions],
  )
  const [nodes, setNodes] = useState(projection.nodes)

  useEffect(() => setNodes(projection.nodes), [projection.nodes])

  const onNodesChange = (changes: NodeChange<BindingCanvasNode>[]) => {
    setNodes((items) => applyNodeChanges(changes, items))
  }

  const persistPositions = (updatedNodes: BindingCanvasNode[]) => {
    onPositionsChange(Object.fromEntries(updatedNodes.map((node) => [
      node.id,
      { x: node.position.x, y: node.position.y },
    ])))
  }

  const createBinding = (connection: Connection) => {
    if (!control) return
    const binding = bindingFromConnection(control, modules, nodes, connection)
    if (!binding) return
    onControlChange({ ...control, bindings: [...control.bindings, binding] })
    onSelectBinding(binding.id)
  }

  const hasSources = nodes.some((node) => node.data.kind === 'owner')
  const hasTargets = nodes.some((node) => node.data.kind === 'target')

  return <div className="binding-canvas">
    <ReactFlow<BindingCanvasNode>
      nodes={nodes}
      edges={projection.edges}
      nodeTypes={bindingNodeTypes}
      onNodesChange={onNodesChange}
      onNodeDragStop={(_, __, selectedNodes) => persistPositions(selectedNodes.length
        ? nodes.map((node) => selectedNodes.find((selected) => selected.id === node.id) ?? node)
        : nodes)}
      onConnect={createBinding}
      isValidConnection={(connection) => Boolean(control
        && bindingFromConnection(control, modules, nodes, connection))}
      onEdgeClick={(_, edge) => onSelectBinding(edge.id)}
      onEdgesDelete={(edges) => {
        if (!control) return
        const deleted = new Set(edges.map((edge) => edge.id))
        onControlChange({
          ...control,
          bindings: control.bindings.filter((binding) => !deleted.has(binding.id)),
        })
        onSelectBinding(null)
      }}
      onPaneClick={() => onSelectBinding(null)}
      defaultEdgeOptions={{ markerEnd: { type: MarkerType.ArrowClosed } }}
      fitView
      minZoom={0.35}
      maxZoom={1.8}
    >
      <Background variant={BackgroundVariant.Dots} gap={22} size={1} color="#cad0d0" />
      <Controls showInteractive={false} />
      <MiniMap pannable zoomable nodeColor={(node) =>
        node.type === 'bindingOwner' ? '#976522' : '#254c46'}
        maskColor="rgba(235, 238, 234, 0.78)" />
    </ReactFlow>
    {!control && <div className="canvas-empty-state">
      <strong>No control project</strong>
      <span>Create one to add semantic endpoints and bindings.</span>
      <button type="button" onClick={() => onControlChange(createEmptyControlProject())}>
        Create control project
      </button>
    </div>}
    {control && (!hasSources || !hasTargets) && <div className="canvas-empty-state">
      <strong>{!hasSources ? 'No semantic endpoints' : 'No controllable parameters'}</strong>
      <span>{!hasSources
        ? 'Add an endpoint to a device or presentation surface.'
        : 'Add a module with a writable automatable parameter.'}</span>
    </div>}
  </div>
}
