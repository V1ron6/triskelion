import { Component } from 'react';

// A page that crashes while rendering shows a message here instead of blanking the whole panel.
export default class ErrorBoundary extends Component {
  state = { failed: false };

  static getDerivedStateFromError() {
    return { failed: true };
  }

  componentDidCatch(error) {
    console.error(error);
  }

  render() {
    if (!this.state.failed) return this.props.children;
    return (
      <div className="banner error" role="alert">
        This page hit an error.{' '}
        <button className="link" onClick={() => this.setState({ failed: false })}>Try again</button>
      </div>
    );
  }
}
