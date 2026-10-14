"""Optional, read-only view of the training worker's current simulation."""

import time


class TrainingViewer:
    def __init__(self, simulation, emit, frames_per_second=30):
        self.simulation = simulation
        self.emit = emit
        self.frame_interval = 1 / frames_per_second
        self.viewer = None
        self.active = False
        self.last_frame = 0.0

    def toggle(self):
        if self.active:
            self.hide()
            return
        try:
            if self.viewer is None:
                import aura_python
                self.viewer = aura_python.AuraViewer(self.simulation)
                self.viewer.set_title("AURA / Live training / V or Esc returns to headless")
            self.viewer.show()
            self.active = True
            self.last_frame = float("-inf")
            self.emit("viewer", 1)
            self.service()
        except Exception as error:
            self.failed(error)

    def hide(self):
        if self.viewer is not None:
            self.viewer.hide()
        self.active = False
        self.emit("viewer", 0)

    def failed(self, error):
        self.active = False
        if self.viewer is not None:
            try:
                self.viewer.hide()
            except Exception:
                pass
        self.emit("viewer", 0)
        self.emit("viewer_error", str(error).replace("\n", " "))

    def service(self):
        """Poll even while paused; rendering never advances the simulation."""
        if not self.active:
            return
        try:
            if not self.viewer.is_open():
                self.hide()
                return
            now = time.monotonic()
            if now - self.last_frame >= self.frame_interval:
                self.viewer.render(self.simulation)
                self.last_frame = now
        except Exception as error:
            self.failed(error)

    def close(self):
        if self.viewer is not None:
            self.viewer.hide()
            self.viewer = None
        self.active = False
