export interface NotificationFilter {
  color?: string | null;
  sound?: number | null;
  display_stats: boolean;
  matched_stat_lines?: number[] | null;
}

export interface ItemDrop {
  unit_id: number;
  class: number;
  item_code?: string;
  itemCode?: string;
  quality: string;
  name: string;
  base_name: string;
  canonical_name?: string;
  canonicalName?: string;
  stats: string;
  is_ethereal: boolean;
  is_identified: boolean;
  is_runeword?: boolean;
  isRuneword?: boolean;
  mode?: number;
  file_index?: number;
  is_hellforged?: boolean;
  isHellforged?: boolean;
  category?: string | null;
  is_relic?: boolean;
  history_pushed?: boolean;
  source?: string;
  name_source?: string;
  is_new_grail?: boolean;
  new_grail_label?: string;
  gsf_needed_by?: string[];
  seed?: number;
  filter?: NotificationFilter | null;
}
