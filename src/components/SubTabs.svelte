<script lang="ts">
  interface SubTab {
    id: string;
    label: string;
  }

  interface Props {
    tabs: SubTab[];
    activeTab?: string;
    ariaLabel?: string;
    onReorder?: (tabIds: string[]) => void;
  }

  let {
    tabs,
    activeTab = $bindable(tabs[0]?.id ?? ''),
    ariaLabel = 'Sections',
    onReorder,
  }: Props = $props();

  let dragState = $state<null | {
    id: string;
    pointerId: number;
    startX: number;
    startY: number;
    dragging: boolean;
  }>(null);
  let suppressClick = false;

  function selectTab(tabId: string): void {
    if (suppressClick) {
      suppressClick = false;
      return;
    }
    activeTab = tabId;
  }

  function reorder(sourceId: string, targetId: string): void {
    if (sourceId === targetId) return;
    const ids = tabs.map((tab) => tab.id);
    const from = ids.indexOf(sourceId);
    const to = ids.indexOf(targetId);
    if (from < 0 || to < 0) return;
    ids.splice(to, 0, ids.splice(from, 1)[0]);
    onReorder?.(ids);
  }

  function handlePointerDown(event: PointerEvent, tabId: string): void {
    if (event.button !== 0) return;
    dragState = {
      id: tabId,
      pointerId: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      dragging: false,
    };
    (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
  }

  function handlePointerMove(event: PointerEvent): void {
    if (!dragState) return;
    const dx = Math.abs(event.clientX - dragState.startX);
    const dy = Math.abs(event.clientY - dragState.startY);
    if (!dragState.dragging && dx + dy >= 6) {
      dragState.dragging = true;
      suppressClick = true;
    }
    if (!dragState.dragging) return;

    const element = document.elementFromPoint(event.clientX, event.clientY);
    const target = element?.closest<HTMLElement>('[data-sub-tab-id]');
    const targetId = target?.dataset.subTabId;
    if (targetId && targetId !== dragState.id) {
      reorder(dragState.id, targetId);
    }
  }

  function handlePointerEnd(event: PointerEvent): void {
    if (dragState?.dragging) suppressClick = true;
    try {
      (event.currentTarget as HTMLElement).releasePointerCapture(event.pointerId);
    } catch {
      // Pointer capture can already be gone if the browser cancelled the pointer.
    }
    dragState = null;
  }
</script>

<div class="sub-tabs-list" role="tablist" aria-label={ariaLabel}>
  {#each tabs as tab (tab.id)}
    <button
      type="button"
      class="sub-tab"
      class:active={activeTab === tab.id}
      class:dragging={dragState?.id === tab.id && dragState.dragging}
      data-sub-tab-id={tab.id}
      aria-selected={activeTab === tab.id}
      role="tab"
      onclick={() => selectTab(tab.id)}
      onpointerdown={(event) => handlePointerDown(event, tab.id)}
      onpointermove={handlePointerMove}
      onpointerup={handlePointerEnd}
      onpointercancel={handlePointerEnd}
    >
      {tab.label}
    </button>
  {/each}
</div>
