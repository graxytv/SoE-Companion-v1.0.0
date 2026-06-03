<script lang="ts">
  import type { Snippet } from 'svelte';
  
  interface Tab {
    id: string;
    label: string;
  }
  
  interface Props {
    tabs: Tab[];
    activeTab?: string;
    onTabChange?: (tabId: string) => void;
    onReorder?: (tabIds: string[]) => void;
    children: Snippet<[string]>;
  }
  
  let {
    tabs,
    activeTab = $bindable(tabs[0]?.id ?? ''),
    onTabChange,
    onReorder,
    children
  }: Props = $props();

  let dragState = $state<null | {
    id: string;
    pointerId: number;
    startX: number;
    startY: number;
    dragging: boolean;
  }>(null);
  let suppressClick = false;
  
  function selectTab(tabId: string) {
    if (suppressClick) {
      suppressClick = false;
      return;
    }
    activeTab = tabId;
    onTabChange?.(tabId);
  }
  
  function handleKeyDown(e: KeyboardEvent, tabId: string) {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      selectTab(tabId);
    }
  }

  function reorder(sourceId: string, targetId: string) {
    if (sourceId === targetId) return;
    const ids = tabs.map((tab) => tab.id);
    const from = ids.indexOf(sourceId);
    const to = ids.indexOf(targetId);
    if (from < 0 || to < 0) return;
    ids.splice(to, 0, ids.splice(from, 1)[0]);
    onReorder?.(ids);
  }

  function handlePointerDown(event: PointerEvent, tabId: string) {
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

  function handlePointerMove(event: PointerEvent) {
    if (!dragState) return;
    const dx = Math.abs(event.clientX - dragState.startX);
    const dy = Math.abs(event.clientY - dragState.startY);
    if (!dragState.dragging && dx + dy >= 6) {
      dragState.dragging = true;
      suppressClick = true;
    }
    if (!dragState.dragging) return;

    const element = document.elementFromPoint(event.clientX, event.clientY);
    const target = element?.closest<HTMLElement>('[data-tab-id]');
    const targetId = target?.dataset.tabId;
    if (targetId && targetId !== dragState.id) {
      reorder(dragState.id, targetId);
    }
  }

  function handlePointerEnd(event: PointerEvent) {
    if (dragState?.dragging) suppressClick = true;
    try {
      (event.currentTarget as HTMLElement).releasePointerCapture(event.pointerId);
    } catch {
      // Pointer capture can already be gone if the browser cancelled the pointer.
    }
    dragState = null;
  }
</script>

<div class="tabs">
  <div class="tabs-list" role="tablist">
    {#each tabs as tab (tab.id)}
      <button
        class="tab"
        class:dragging={dragState?.id === tab.id && dragState.dragging}
        class:active={activeTab === tab.id}
        data-tab-id={tab.id}
        role="tab"
        aria-selected={activeTab === tab.id}
        tabindex={activeTab === tab.id ? 0 : -1}
        onclick={() => selectTab(tab.id)}
        onkeydown={(e) => handleKeyDown(e, tab.id)}
        onpointerdown={(e) => handlePointerDown(e, tab.id)}
        onpointermove={handlePointerMove}
        onpointerup={handlePointerEnd}
        onpointercancel={handlePointerEnd}
      >
        {tab.label}
      </button>
    {/each}
  </div>
  
  <div class="tabs-content" role="tabpanel">
    {#key activeTab}
      <div class="tab-panel-transition">
        {@render children(activeTab)}
      </div>
    {/key}
  </div>
</div>
