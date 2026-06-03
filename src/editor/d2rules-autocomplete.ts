/**
 * CodeMirror 6 autocompletion factory for PD2/SoE loot filter syntax.
 *
 * Usage: d2rulesAutocomplete(() => itemsDictionaryStore.options)
 * Returns a CodeMirror Extension.
 */
import {
  autocompletion,
  type CompletionContext,
  type CompletionResult,
} from "@codemirror/autocomplete";
import type { Extension } from "@codemirror/state";
import type { AutocompleteOption } from "../stores/items-dictionary.svelte";

const CONDITIONS = [
  { label: "UNI", detail: "Unique quality" },
  { label: "SET", detail: "Set quality" },
  { label: "RARE", detail: "Rare quality" },
  { label: "MAG", detail: "Magic quality" },
  { label: "NMAG", detail: "Non-magic (unid unique/set)" },
  { label: "INF", detail: "Inferior quality" },
  { label: "NOR", detail: "Normal quality" },
  { label: "SUP", detail: "Superior quality" },
  { label: "ID", detail: "Identified" },
  { label: "!ID", detail: "Unidentified" },
  { label: "ETH", detail: "Ethereal" },
  { label: "!ETH", detail: "Non-ethereal" },
  { label: "SOCK", detail: "Has sockets" },
  { label: "!SOCK", detail: "No sockets" },
  { label: "RUNE", detail: "Is a rune" },
  { label: "GEM", detail: "Is a gem" },
  { label: "2H", detail: "Two-handed weapon" },
  { label: "QUESTITEM", detail: "Quest item" },
  { label: "4_STAR_UNIQUE", detail: "4-star unique (endgame)" },
  { label: "3_STAR_UNIQUE", detail: "3-star unique" },
  { label: "2_STAR_UNIQUE", detail: "2-star unique" },
  { label: "3_STAR_SET", detail: "3-star set item" },
  { label: "2_STAR_SET", detail: "2-star set item" },
  { label: "SOCK0", detail: "0 sockets" },
  { label: "SOCK1", detail: "1 socket" },
  { label: "SOCK2", detail: "2 sockets" },
  { label: "SOCK3", detail: "3 sockets" },
  { label: "SOCK4", detail: "4 sockets" },
  { label: "SOCK5", detail: "5 sockets" },
  { label: "SOCK6", detail: "6 sockets" },
];

const DISPLAY_TOKENS = [
  { label: "%NAME%", detail: "Item name" },
  { label: "%ILVL%", detail: "Item level" },
  { label: "%CLVL%", detail: "Character level" },
  { label: "%ALVL%", detail: "Area level" },
  { label: "%GOLD%", detail: "Gold/unique color" },
  { label: "%GREEN%", detail: "Green/set color" },
  { label: "%BLUE%", detail: "Blue/magic color" },
  { label: "%RED%", detail: "Red color" },
  { label: "%WHITE%", detail: "White color" },
  { label: "%GRAY%", detail: "Gray color" },
  { label: "%YELLOW%", detail: "Yellow color" },
  { label: "%ORANGE%", detail: "Orange color" },
  { label: "%PURPLE%", detail: "Purple color" },
  { label: "%PINK%", detail: "Pink color" },
  { label: "%MAP-58%", detail: "Minimap icon — gold star" },
  { label: "%MAP-3%", detail: "Minimap icon — green" },
  { label: "%MAP-1%", detail: "Minimap icon (replace N)" },
  { label: "%BORDER-55%", detail: "Item border — gold" },
  { label: "%BORDER-46%", detail: "Item border — orange" },
  { label: "{%NAME%}", detail: "Notification popup with item name" },
];

function completionSource(
  getOptions: () => AutocompleteOption[],
): (context: CompletionContext) => CompletionResult | null {
  return (context: CompletionContext): CompletionResult | null => {
    const line = context.state.doc.lineAt(context.pos);
    const lineText = line.text;
    const col = context.pos - line.from;
    const before = lineText.slice(0, col);

    // Skip comment lines
    if (lineText.trimStart().startsWith("//") || lineText.trimStart().startsWith("#")) {
      return null;
    }

    // After ]: — offer display tokens and item names
    const colonIdx = lineText.indexOf("]:");
    if (colonIdx !== -1 && col > colonIdx + 2) {
      // Token starting with % or {
      const tokenStart = before.lastIndexOf("%");
      const braceStart = before.lastIndexOf("{");
      const from = Math.max(tokenStart, braceStart);

      if (from !== -1 && from >= colonIdx) {
        return {
          from: line.from + from,
          options: DISPLAY_TOKENS.map((t) => ({
            label: t.label,
            detail: t.detail,
            type: "keyword",
          })),
        };
      }

      // Offer display tokens on space / explicit trigger
      if (context.explicit || before.slice(-1) === " ") {
        return {
          from: context.pos,
          options: DISPLAY_TOKENS.map((t) => ({
            label: t.label,
            detail: t.detail,
            type: "keyword",
          })),
        };
      }

      return null;
    }

    // Inside [ ] — offer condition keywords + item names from dictionary
    const openBracket = lineText.lastIndexOf("[", col);
    const closeBracket = lineText.indexOf("]", openBracket === -1 ? 0 : openBracket);
    if (openBracket !== -1 && (closeBracket === -1 || col <= closeBracket)) {
      const word = context.matchBefore(/[\w!_]*/);
      const dictOptions = getOptions().map((o) => ({
        label: o.label,
        detail: o.kind,
        type: "variable",
      }));
      return {
        from: word ? word.from : context.pos,
        options: [...CONDITIONS.map((c) => ({ label: c.label, detail: c.detail, type: "keyword" as const })), ...dictOptions],
      };
    }

    // Start of line — offer ItemDisplay
    const word = context.matchBefore(/\w*/);
    if (!word || (word.from === word.to && !context.explicit)) return null;
    if ("itemdisplay".startsWith(word.text.toLowerCase())) {
      return {
        from: word.from,
        options: [{ label: "ItemDisplay[", detail: "Start a filter rule", type: "keyword" }],
      };
    }

    return null;
  };
}

/**
 * Returns a CodeMirror Extension that provides PD2/SoE filter syntax autocompletion.
 * @param getOptions  Callback returning current item dictionary options.
 */
export function d2rulesAutocomplete(
  getOptions: () => AutocompleteOption[],
): Extension {
  return autocompletion({
    override: [completionSource(getOptions)],
  });
}
