import "../learn.css";
import PoleFieldGuide from "./PoleFieldGuide";

export const metadata = {
  title: "Painting With Poles: 14-Pole Z-Plane Morphing & Burg LPC · Human Being Audio",
  description:
    "Interactive two-layer VST & DSP Field Guide for Pole Field: drag poles and zeros on a live unit circle, prove why log-polar beats naive biquad morphing, A/B ADAA2 anti-aliasing, and run live 14th-order Burg LPC audio imprinting in your browser.",
  alternates: { canonical: "/learn/pole-field" },
};

export default function PoleFieldGuidePage() {
  return <PoleFieldGuide />;
}
