import { describe, expect, it } from 'vitest'
import { projectFilename } from './project-api'

describe('hosted project filenames', () => {
  it('keeps readable application names within the host filename contract', () => {
    expect(projectFilename('Studio Rig')).toBe('Studio Rig.json')
    expect(projectFilename('../Live/Set')).toBe('Live-Set.json')
    expect(projectFilename('  ')).toBe('musicrat-application.json')
  })
})