import type { ModuleDescriptor } from './model'

const implementationModuleClasses = new Set([
  'MusicRaTActionMapper',
  'MusicRaTChannelMapper',
  'MusicRaTControlFeedbackRouter',
  'MusicRaTControlMapper',
  'MusicRaTControlSource',
  'MusicRaTDeckControlSource',
  'MusicRaTLevelMeterUiAdapter',
  'MusicRaTLvglWidgetSink',
  'MusicRaTNullSink',
  'MusicRaTParameterSource',
  'MusicRaTParameterStateUiAdapter',
  'MusicRaTPlaybackStatusUiAdapter',
  'MusicRaTTelemetrySink',
  'MusicRaTTransportSource',
  'MusicRaTWidgetUpdateMerger',
])

export function isImplementationModule(descriptor: ModuleDescriptor): boolean {
  return implementationModuleClasses.has(descriptor.module_class)
}

export function moduleDisplayName(descriptor: ModuleDescriptor): string {
  return descriptor.module_class
    .replace(/^MusicRaT/, '')
    .replace(/([a-z0-9])([A-Z])/g, '$1 $2')
}

export function modulePickerEntries(
  descriptors: ModuleDescriptor[],
  query: string,
  showImplementationModules: boolean,
): ModuleDescriptor[] {
  const normalizedQuery = query.trim().toLocaleLowerCase()
  return descriptors.filter((descriptor) =>
    (showImplementationModules || !isImplementationModule(descriptor))
    && moduleDisplayName(descriptor).toLocaleLowerCase().includes(normalizedQuery))
}
