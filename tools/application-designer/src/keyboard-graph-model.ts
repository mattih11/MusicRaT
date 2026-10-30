import type { Edge, Node } from '@xyflow/react'
import {
  keyboardNoteRoutes,
  keyboardNoteTargets,
  noteInputTargets,
  type DesignerWorkspace,
  type MusicRaTControlProject,
  type NoteTarget,
} from './model'

export interface KeyboardNodeData extends Record<string, unknown> {
  surface_id: string
  widget_id: string
  display_name: string
  surface_name: string
}

export type KeyboardNode = Node<KeyboardNodeData, 'keyboard'>

export function projectKeyboardGraph(workspace: DesignerWorkspace): {
  nodes: KeyboardNode[]
  edges: Edge[]
} {
  const nodes: KeyboardNode[] = (workspace.source.musicrat_control?.surfaces ?? [])
    .filter((surface) => surface.target === 'lvgl')
    .flatMap((surface) => surface.widgets.filter((widget) => widget.kind === 'keyboard')
      .map((widget) => ({ surface, widget })))
    .map(({ surface, widget }, index) => {
      let id = `keyboard:${JSON.stringify([surface.id, widget.id])}`
      while (workspace.modules.some((module) => module.id === id)) id = `:${id}`
      return {
        id,
        type: 'keyboard',
        deletable: false,
        position: workspace.source.musicrat_designer?.keyboard_positions?.[id]
          ?? { x: -260, y: 80 + index * 180 },
        data: {
          surface_id: surface.id,
          widget_id: widget.id,
          display_name: widget.display_name,
          surface_name: surface.display_name,
        },
      }
    })
  const edges = keyboardNoteRoutes(workspace.source.musicrat_control, workspace.modules)
    .map(({ surface, widget, target }): Edge => {
      const source = nodes.find((node) =>
        node.data.surface_id === surface.id && node.data.widget_id === widget.id)!.id
      return {
        id: `keyboard-route:${JSON.stringify([surface.id, widget.id, target.module_id, target.port_id])}`,
        source,
        sourceHandle: 'note_events',
        target: target.module_id,
        targetHandle: target.port_id,
        className: 'edge-note',
      }
    })
  return { nodes, edges }
}

export function connectKeyboard(
  workspace: DesignerWorkspace,
  sourceId: string,
  target: NoteTarget,
): MusicRaTControlProject {
  const source = projectKeyboardGraph(workspace).nodes.find((node) => node.id === sourceId)
  if (!source) throw new Error('Keyboard source does not exist.')
  if (!noteInputTargets(workspace.modules).some((candidate) =>
    candidate.module_id === target.module_id && candidate.port_id === target.port_id)) {
    throw new Error('Keyboards connect only to compatible note-event synchronized inputs.')
  }
  if (workspace.connections.some((connection) =>
    connection.target_module_id === target.module_id && connection.target_port_id === target.port_id)
      || keyboardNoteRoutes(workspace.source.musicrat_control, workspace.modules).some((route) =>
        route.target.module_id === target.module_id && route.target.port_id === target.port_id)) {
    throw new Error('The target port already has a connection.')
  }
  const control = workspace.source.musicrat_control!
  return {
    ...control,
    surfaces: control.surfaces.map((surface) => surface.id !== source.data.surface_id ? surface : {
      ...surface,
      widgets: surface.widgets.map((widget) => widget.id !== source.data.widget_id ? widget : {
        ...widget,
        note_target_module_id: undefined,
        note_targets: [...keyboardNoteTargets(widget, workspace.modules), target],
      }),
    }),
  }
}

export function disconnectKeyboardEdges(
  workspace: DesignerWorkspace,
  edgeIds: string[],
): MusicRaTControlProject {
  const control = workspace.source.musicrat_control
  if (!control) throw new Error('Keyboard control project does not exist.')
  const graph = projectKeyboardGraph(workspace)
  const removed = graph.edges.filter((edge) => edgeIds.includes(edge.id))
  return {
    ...control,
    surfaces: control.surfaces.map((surface) => ({
      ...surface,
      widgets: surface.widgets.map((widget) => {
        const node = graph.nodes.find((candidate) =>
          candidate.data.surface_id === surface.id && candidate.data.widget_id === widget.id)
        if (!node || !removed.some((edge) => edge.source === node.id)) return widget
        return {
          ...widget,
          note_target_module_id: undefined,
          note_targets: keyboardNoteTargets(widget, workspace.modules).filter((target) =>
            !removed.some((edge) => edge.source === node.id
              && edge.target === target.module_id && edge.targetHandle === target.port_id)),
        }
      }),
    })),
  }
}
