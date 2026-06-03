export function stripMxlTierSuffix(name: string): string {
  return name.replace(/\s+(?:TU|SU|SSU|SSSU)$/i, "").trim();
}

