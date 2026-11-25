import type { Metadata } from "next";
import Link from "next/link";
import studio from "../../studio.json";
import Image from "next/image";
import mark from "../../design/studio/mark.svg";
import "./globals.css";
const productNames = new Intl.ListFormat("en").format(
  Object.values(studio.products).map((product) => product.name),
);

export const metadata: Metadata = {
  metadataBase: new URL(studio.website),
  title: "Human Being Audio · Free audio plugins",
  description: `${productNames}. Open-source audio effects and instruments for macOS, Windows and Ubuntu.`,
  alternates: { canonical: "/" },
  icons: { icon: mark.src },
};
export default function Layout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">

      <body id="top">
        <a className="skip" href="#main">
          Skip to content
        </a>
        <header className="site-header">
          <div className="wrap site-header__bar">
            <Link className="wordmark" href="/">
              <Image src={mark} alt="" className="wordmark__mark" width={24} height={24} unoptimized />
              <span>Human Being <b>Audio</b></span>
            </Link>
            <nav aria-label="Primary">
              <ul className="nav">
                <li>
                  <a href="/#plugins">Plugins</a>
                </li>
                <li>
                  <a href="/#notes">Releases</a>
                </li>
                <li>
                  <a href="/#install">Install</a>
                </li>
                <li>
                  <Link href="/learn">Learn</Link>
                </li>
                <li>
                  <a href={studio.repository}>GitHub ↗</a>
                </li>
              </ul>
            </nav>
          </div>
        </header>
        {children}
        <footer className="site-footer">
          <div className="wrap studio-footer">
            <strong>{studio.publisher}</strong>
            <Link href="/learn">DSP Field Guides</Link>
            <a href={studio.repository}>Studio source ↗</a>
            <a href={studio.repository + "/issues"}>Report a problem ↗</a>
          </div>
        </footer>
      </body>
    </html>
  );
}
