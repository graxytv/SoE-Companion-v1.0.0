/// <reference types="svelte" />

declare const __APP_VERSION__: string;

declare module "*.css";
declare module "*.png" {
  const src: string;
  export default src;
}
declare module "*.jpg" {
  const src: string;
  export default src;
}
declare module "*.webp" {
  const src: string;
  export default src;
}
