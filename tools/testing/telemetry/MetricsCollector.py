"""
Metrics Collection System for DarkAges MMO Testing

This module provides comprehensive metrics collection for all three
testing tiers: Unit, Simulation, and Real execution.
"""

import json
import time
import uuid
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Callable
import threading


@dataclass
class MetricPoint:
    """Single metric data point."""
    name: str
    value: float
    timestamp: float
    tags: Dict[str, str]
    
    def to_dict(self) -> Dict:
        return {
            "name": self.name,
            "value": self.value,
            "timestamp": self.timestamp,
            "tags": self.tags
        }


@dataclass
class Event:
    """Single event record."""
    name: str
    timestamp: float
    data: Dict[str, Any]
    level: str = "INFO"
    trace_id: str = ""
    
    def to_dict(self) -> Dict:
        return {
            "name": self.name,
            "timestamp": self.timestamp,
            "data": self.data,
            "level": self.level,
            "trace_id": self.trace_id
        }


class MetricsCollector:
    """Collects and aggregates metrics during testing."""
    
    def __init__(
        self,
        test_name: str,
        output_dir: str = "tools/testing/reports",
        enable_realtime: bool = True
    ):
        self.test_name = test_name
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        self.session_id = str(uuid.uuid4())[:8]
        self.start_time = time.time()
        
        # Storage
        self.metrics: List[MetricPoint] = []
        self.events: List[Event] = []
        self.traces: Dict[str, Dict] = {}
        
        # Aggregation caches
        self._counters: Dict[str, float] = defaultdict(float)
        self._gauges: Dict[str, float] = {}
        self._histograms: Dict[str, List[float]] = defaultdict(list)
        
        # Thread safety
        self._lock = threading.Lock()
        
        # Real-time streaming
        self._realtime_enabled = enable_realtime
        self._subscribers: List[Callable] = []
        
        # Start session
        self.record_event("test_session_start", {
            "test_name": test_name,
            "session_id": self.session_id,
            "start_time": self.start_time
        })
    
    def record_metric(
        self,
        name: str,
        value: float,
        tags: Optional[Dict[str, str]] = None
    ) -> None:
        """Record a metric value."""
        with self._lock:
            point = MetricPoint(
                name=name,
                value=value,
                timestamp=time.time(),
                tags=tags or {}
            )
            self.metrics.append(point)
            
            # Update aggregations
            if name.startswith("counter."):
                self._counters[name] += value
            else:
                self._gauges[name] = value
                self._histograms[name].append(value)
        
        if self._realtime_enabled:
            self._notify_subscribers("metric", point.to_dict())
    
    def increment_counter(
        self,
        name: str,
        value: float = 1.0,
        tags: Optional[Dict[str, str]] = None
    ) -> None:
        """Increment a counter metric."""
        self.record_metric(f"counter.{name}", value, tags)
    
    def record_gauge(
        self,
        name: str,
        value: float,
        tags: Optional[Dict[str, str]] = None
    ) -> None:
        """Record a gauge metric."""
        self.record_metric(f"gauge.{name}", value, tags)
    
    def record_timing(
        self,
        name: str,
        duration_ms: float,
        tags: Optional[Dict[str, str]] = None
    ) -> None:
        """Record a timing metric."""
        self.record_metric(f"timing.{name}", duration_ms, tags)
    
    def record_event(
        self,
        name: str,
        data: Dict[str, Any],
        level: str = "INFO",
        trace_id: str = ""
    ) -> None:
        """Record an event."""
        with self._lock:
            event = Event(
                name=name,
                timestamp=time.time(),
                data=data,
                level=level,
                trace_id=trace_id or str(uuid.uuid4())[:8]
            )
            self.events.append(event)
        
        if self._realtime_enabled:
            self._notify_subscribers("event", event.to_dict())
    
    def log_debug(self, name: str, data: Dict[str, Any], trace_id: str = "") -> None:
        self.record_event(name, data, "DEBUG", trace_id)
    
    def log_info(self, name: str, data: Dict[str, Any], trace_id: str = "") -> None:
        self.record_event(name, data, "INFO", trace_id)
    
    def log_warn(self, name: str, data: Dict[str, Any], trace_id: str = "") -> None:
        self.record_event(name, data, "WARN", trace_id)
    
    def log_error(self, name: str, data: Dict[str, Any], trace_id: str = "") -> None:
        self.record_event(name, data, "ERROR", trace_id)
    
    def start_trace(self, operation: str, trace_id: str = "") -> str:
        """Start a new distributed trace."""
        trace_id = trace_id or str(uuid.uuid4())[:12]
        
        with self._lock:
            self.traces[trace_id] = {
                "operation": operation,
                "start_time": time.time(),
                "spans": []
            }
        
        return trace_id
    
    def end_trace(self, trace_id: str) -> None:
        """End a trace and record total duration."""
        with self._lock:
            if trace_id in self.traces:
                trace = self.traces[trace_id]
                duration = (time.time() - trace["start_time"]) * 1000
                trace["total_duration_ms"] = duration
                trace["end_time"] = time.time()
                
                self.record_timing(
                    f"trace.{trace['operation']}",
                    duration,
                    {"trace_id": trace_id}
                )
    
    def get_statistics(self, metric_name: str) -> Dict[str, float]:
        """Calculate statistics for a metric."""
        with self._lock:
            values = [m.value for m in self.metrics if m.name == metric_name]
        
        if not values:
            return {}
        
        values.sort()
        n = len(values)
        mean = sum(values) / n
        
        return {
            "count": n,
            "min": values[0],
            "max": values[-1],
            "mean": mean,
            "median": values[n // 2] if n % 2 else (values[n // 2 - 1] + values[n // 2]) / 2,
            "p95": values[int(n * 0.95)] if n > 20 else values[-1],
            "p99": values[int(n * 0.99)] if n > 100 else values[-1],
            "std_dev": (sum((x - mean) ** 2 for x in values) / n) ** 0.5
        }
    
    def _notify_subscribers(self, event_type: str, data: Dict) -> None:
        """Notify all subscribers."""
        for callback in self._subscribers:
            try:
                callback(event_type, data)
            except Exception:
                pass
    
    def generate_report(self) -> Dict[str, Any]:
        """Generate comprehensive test report."""
        with self._lock:
            end_time = time.time()
            duration = end_time - self.start_time
            
            metric_names = set(m.name for m in self.metrics)
            statistics = {
                name: self.get_statistics(name)
                for name in metric_names
            }
            
            event_counts = defaultdict(int)
            for event in self.events:
                event_counts[event.level] += 1
            
            errors = [e.to_dict() for e in self.events if e.level == "ERROR"]
            
            return {
                "test_name": self.test_name,
                "session_id": self.session_id,
                "start_time": self.start_time,
                "end_time": end_time,
                "duration_seconds": duration,
                "summary": {
                    "total_metrics": len(self.metrics),
                    "total_events": len(self.events),
                    "total_traces": len(self.traces),
                    "event_breakdown": dict(event_counts),
                    "error_count": len(errors)
                },
                "statistics": statistics,
                "errors": errors,
                "traces": self.traces
            }
    
    def save_report(self, filename: str = None) -> str:
        """Save report to file."""
        if filename is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"{self.test_name}_{self.session_id}_{timestamp}.json"
        
        filepath = self.output_dir / filename
        report = self.generate_report()
        
        with open(filepath, 'w') as f:
            json.dump(report, f, indent=2)
        
        return str(filepath)
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Auto-save report on exit."""
        if exc_type is not None:
            self.log_error("test_exception", {
                "type": str(exc_type),
                "value": str(exc_val)
            })
        
        self.record_event("test_session_end", {
            "duration": time.time() - self.start_time,
            "error": exc_type is not None
        })
        
        report_path = self.save_report()
        print(f"[MetricsCollector] Report saved: {report_path}")
        return False
