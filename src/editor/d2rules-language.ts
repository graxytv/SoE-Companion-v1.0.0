/**
 * CodeMirror 6 language definition for PD2/SoE loot filter syntax.
 *
 * PD2 filter format: ItemDisplay[CONDITIONS]: display string
 * Conditions are space-separated keywords inside [ ]
 * Display string uses %TOKEN% variables and {%NAME%} for notifications
 */
import { StreamLanguage, LanguageSupport } from "@codemirror/language";
import { Tag } from "@lezer/highlight";

export const d2rulesTags = {
  tier: Tag.define(),
  quality: Tag.define(),
  ethereal: Tag.define(),
  socket: Tag.define(),
  action: Tag.define(),
  notification: Tag.define(),
  directive: Tag.define(),
  groupBracket: Tag.define(),
  unknown: Tag.define(),
};

// PD2 quality keywords
const QUALITY_KEYWORDS = [
  "uni", "set", "rare", "mag", "nmag", "inf", "nor", "sup",
  "unique", "magic", "normal", "superior", "inferior",
  "craft", "crafted",
];

// PD2 tier/star keywords
const TIER_KEYWORDS = [
  "4_star_unique", "3_star_unique", "2_star_unique",
  "3_star_set", "2_star_set",
  "sacred", "eth",
];

// PD2 condition keywords
const CONDITION_KEYWORDS = [
  "id", "!id", "eth", "!eth", "sock", "!sock",
  "rune", "gem", "staff", "2h", "questitem",
  "sock0", "sock1", "sock2", "sock3", "sock4", "sock5", "sock6",
];

// PD2 display tokens (used after the colon)
const TOKEN_KEYWORDS = [
  "%name%", "%ilvl%", "%clvl%", "%alvl%", "%gold%", "%green%",
  "%blue%", "%red%", "%white%", "%gray%", "%grey%", "%yellow%",
  "%orange%", "%purple%", "%pink%", "%black%",
];

const COLOR_KEYWORDS = [
  "white", "red", "lime", "blue", "gold", "grey", "gray",
  "black", "pink", "orange", "yellow", "green", "purple",
];

const VISIBILITY_KEYWORDS = ["show", "hide"];
const NOTIFY_KEYWORDS = ["notify"];
const SOUND_KEYWORD_REGEX = /^sound(_none|_?\d+)?$/;

function isSoundKeyword(word: string): boolean {
  return SOUND_KEYWORD_REGEX.test(word);
}

const DISPLAY_KEYWORDS = ["stat"];
const MODIFIER_KEYWORDS = ["eth"];
const MAP_KEYWORDS = ["map"];

// Match %...% tokens (PD2 format like %NAME%, %MAP-58%, %BORDER-55%)
const TOKEN_REGEX = /^%[A-Za-z0-9_\-]+%/;
// Match {%...%} notification tokens
const NOTIF_REGEX = /^\{%[A-Za-z0-9_\-]+%\}/;

const d2rulesLanguage = StreamLanguage.define({
  name: "d2rules",

  tokenTable: {
    tier: d2rulesTags.tier,
    quality: d2rulesTags.quality,
    modifier: d2rulesTags.ethereal,
    socket: d2rulesTags.socket,
    visibility: d2rulesTags.action,
    notify: d2rulesTags.notification,
    color: d2rulesTags.notification,
    sound: d2rulesTags.notification,
    display: d2rulesTags.notification,
    map: d2rulesTags.notification,
    directive: d2rulesTags.directive,
    unknown: d2rulesTags.unknown,
    groupBracket: d2rulesTags.groupBracket,
  },

  token(stream) {
    if (stream.eatSpace()) return null;

    // Comments: // or #
    if (stream.match(/^\/\/.*/)) return "comment";
    if (stream.match(/^#.*/)) return "comment";

    // ItemDisplay keyword — the whole "ItemDisplay" part
    if (stream.match(/^ItemDisplay/)) return "keyword directive";

    // %TOKEN% display tokens
    if (stream.peek() === "%") {
      if (stream.match(TOKEN_REGEX)) return "keyword notify";
      stream.next();
      return null;
    }

    // {%TOKEN%} notification tokens
    if (stream.peek() === "{") {
      if (stream.match(NOTIF_REGEX)) return "atom";
      stream.next();
      return "bracket groupBracket";
    }

    if (stream.peek() === "}") {
      stream.next();
      return "bracket groupBracket";
    }

    // [ and ] condition brackets
    if (stream.peek() === "[") {
      stream.next();
      return "bracket groupBracket";
    }
    if (stream.peek() === "]") {
      stream.next();
      return "bracket groupBracket";
    }

    // : separator
    if (stream.peek() === ":") {
      stream.next();
      return "punctuation";
    }

    // ! prefix for negation
    if (stream.peek() === "!") {
      stream.next();
      if (stream.match(/^\w+/)) {
        const word = "!" + stream.current().slice(1).toLowerCase();
        if (CONDITION_KEYWORDS.includes(word)) return "keyword modifier";
      }
      return null;
    }

    // Words
    if (stream.match(/^\w+/)) {
      const word = stream.current().toLowerCase();

      if (word === "default") {
        const prefix = stream.string.slice(0, stream.start).trim().toLowerCase();
        if (prefix === "hide" || prefix === "show") return "keyword directive";
        return "keyword unknown";
      }

      if (QUALITY_KEYWORDS.includes(word)) return "keyword quality";
      if (TIER_KEYWORDS.includes(word)) return "keyword tier";
      if (CONDITION_KEYWORDS.includes(word)) return "keyword modifier";
      if (VISIBILITY_KEYWORDS.includes(word)) return "keyword visibility";
      if (NOTIFY_KEYWORDS.includes(word)) return "keyword notify";
      if (COLOR_KEYWORDS.includes(word)) return "keyword color";
      if (isSoundKeyword(word)) return "keyword sound";
      if (DISPLAY_KEYWORDS.includes(word)) return "keyword display";
      if (MODIFIER_KEYWORDS.includes(word)) return "keyword modifier";
      if (MAP_KEYWORDS.includes(word)) return "keyword map";

      return "keyword unknown";
    }

    stream.next();
    return null;
  },

  languageData: {
    commentTokens: { line: "//" },
  },
});

/**
 * Create a LanguageSupport instance for PD2/SoE loot filter DSL
 */
export function d2rules(): LanguageSupport {
  return new LanguageSupport(d2rulesLanguage);
}

export { d2rulesLanguage };
