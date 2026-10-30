import { useMemo, useState } from 'react'
import {
  Background, BackgroundVariant, Controls, MiniMap, ReactFlow, type NodeChange,
} from '@xyflow/react'
import type { DesignerPosition, DesignerWorkspace, ModuleDescriptor } from './model'
import {
  connectGraph, disconnectGraph, moveGraphNodes, projectUnifiedGraph, removeGraphModules,
  type GraphEdge, type GraphNode, type GraphNodeData, type GraphRoute,
} from './unified-graph-model'
import { UnifiedGraphNode } from './UnifiedGraphNode'

const nodeTypes = { unified: UnifiedGraphNode }

export interface GraphSelection {
  node?: GraphNodeData
  route?: GraphRoute
}

export function UnifiedGraph({ workspace, catalog, showImplementation, showAdvancedPorts,
  onChange, onSelect, onMessage }: {
  workspace: DesignerWorkspace
  catalog: ModuleDescriptor[]
  showImplementation: boolean
  showAdvancedPorts: boolean
  onChange: (workspace: DesignerWorkspace) => void
  onSelect: (selection: GraphSelection) => void
  onMessage: (message: string) => void
}) {
  const projection = useMemo(() => projectUnifiedGraph(workspace, {
    showImplementation, showAdvancedPorts, catalog,
  }), [workspace, showImplementation, showAdvancedPorts, catalog])
  const [measurements, setMeasurements] = useState<Record<string, { width: number; height: number }>>({})
  const [selectedNodes, setSelectedNodes] = useState<string[]>([])
  const [selectedEdges, setSelectedEdges] = useState<string[]>([])
  const onNodesChange = (changes: NodeChange<GraphNode>[]) => {
    const positions: Record<string, DesignerPosition> = {}
    for (const change of changes) {
      if (change.type === 'dimensions' && change.dimensions) {
        const dimensions = change.dimensions
        setMeasurements((items) => items[change.id]?.width === dimensions.width
          && items[change.id]?.height === dimensions.height ? items : { ...items, [change.id]: dimensions })
      }
      if (change.type === 'position' && change.position) positions[change.id] = change.position
    }
    if (Object.keys(positions).length) onChange(moveGraphNodes(workspace, positions))
  }
  return <div className="unified-graph">
    <ReactFlow<GraphNode, GraphEdge>
      nodes={projection.nodes.map((node) => ({
        ...node, measured: measurements[node.id], selected: selectedNodes.includes(node.id),
      }))}
      edges={projection.edges.map((edge) => ({
        ...edge, selected: selectedEdges.includes(edge.id), deletable: edge.data?.route.kind !== 'generated',
      }))}
      nodeTypes={nodeTypes}
      onNodesChange={onNodesChange}
      onEdgesChange={(changes) => {
        setSelectedEdges((ids) => {
          const next = new Set(ids)
          for (const change of changes) if (change.type === 'select') {
            if (change.selected) next.add(change.id)
            else next.delete(change.id)
          }
          return [...next]
        })
      }}
      onSelectionChange={({ nodes, edges }) => {
        setSelectedNodes(nodes.map((node) => node.id))
        setSelectedEdges(edges.map((edge) => edge.id))
      }}
      onNodeClick={(_, node) => onSelect({ node: node.data })}
      onEdgeClick={(_, edge) => onSelect({ route: edge.data?.route })}
      onPaneClick={() => onSelect({})}
      onConnect={(connection) => {
        try {
          const result = connectGraph(workspace, projection, connection)
          onChange(result.workspace)
          onSelect(result.bindingId ? { route: { kind: 'binding', id: result.bindingId } } : {})
          onMessage('Connection added. Runtime adapters will be generated automatically.')
        } catch (error) {
          onMessage(error instanceof Error ? error.message : 'Could not create connection.')
        }
      }}
      onDelete={({ nodes, edges }) => {
        const disconnected = disconnectGraph(workspace, edges.flatMap((edge) =>
          edge.data ? [edge.data.route] : []))
        onChange(removeGraphModules(disconnected, nodes.flatMap((node) =>
          node.data.module_id && !node.data.generated ? [node.data.module_id] : [])))
        onSelect({})
      }}
      deleteKeyCode={['Delete', 'Backspace']}
      fitView minZoom={0.2} maxZoom={1.8}
    >
      <Background variant={BackgroundVariant.Dots} gap={22} size={1} color="#cad0d0" />
      <Controls showInteractive={false} />
      <MiniMap pannable zoomable nodeColor={(node) => node.data.kind === 'ui' ? '#4e729f'
        : node.data.kind === 'hardware' ? '#b18138'
          : node.data.kind === 'implementation' ? '#8a8294' : '#254c46'} />
    </ReactFlow>
    {projection.warning && <div className="graph-warning" role="status">{projection.warning}</div>}
  </div>
}
