import { type ReactNode } from 'react';
import './Layout.css';

interface LayoutProps {
  children: ReactNode;
}

export function Layout({ children }: LayoutProps) {

  return (
    <div className="layout">
      <main className="main-content">
        <div className="content">
          {children}
        </div>
      </main>
    </div>
  );
}