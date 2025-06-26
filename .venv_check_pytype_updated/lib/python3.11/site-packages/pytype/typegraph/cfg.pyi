from typing import Any, Optional, Iterable, overload

BindingData: Any

class Program:
  cfg_nodes: list[CFGNode]
  variables: list[Variable]
  entrypoint: CFGNode
  next_variable_id: int
  next_binding_id: int

  def NewCFGNode(self, name: Optional[str] = ..., condition: Binding = ...) -> CFGNode: ...
  def NewVariable(self, bindings: Optional[Iterable[BindingData]] = ..., source_set: Optional[Iterable[Binding]] = ..., where: Optional[CFGNode] = ...) -> Variable: ...
  def is_reachable(self, src: CFGNode, dst: CFGNode) -> bool: ...
  def calculate_metrics(self) -> Metrics: ...

class CFGNode:
  id: int
  incoming: list[CFGNode]
  outgoing: list[CFGNode]
  bindings: list[Binding]
  name: str
  condition: Optional[Binding]

  def ConnectNew(self, name: Optional[str] = ..., condition: Optional[Binding] = ...) -> CFGNode: ...
  def ConnectTo(self, node: CFGNode) -> None: ...
  def HasCombination(self, attrs: list[Binding]) -> bool: ...
  def CanHaveCombination(self, attrs: list[Binding]) -> bool: ...

class Variable:
  id: int
  bindings: list[Binding]
  data: list[Any]
  program: Program

  def Bindings(self, cfg_node: CFGNode, strict: Optional[bool] = ...) -> list[Binding]: ...
  def Data(self, cfg_node: CFGNode) -> list[Any]: ...
  def Filter(self, cfg_node: CFGNode, strict: Optional[bool] = ...) -> list[Binding]: ...
  def FilteredData(self, cfg_node: CFGNode) -> list[Any]: ...
  @overload
  def AddBinding(self, data: Any) -> Binding: ...
  @overload
  def AddBinding(self, data: Any, source_set: Iterable[Binding], where: CFGNode) -> Binding: ...
  def AssignToNewVariable(self, where: Optional[CFGNode] = ...) -> Variable: ...
  def PasteVariable(self, variable: Variable, where: Optional[CFGNode] = ..., additional_sources: Optional[Iterable[Binding]] = ...) -> None: ...
  def PasteBinding(self, binding: Binding, where: Optional[CFGNode] = ..., additional_sources: Optional[Iterable[Binding]] = ...) -> None: ...

class Binding:
  id: int
  variable: Variable
  data: Any
  origins: list[Origin]

  def IsVisible(self, where: CFGNode) -> bool: ...
  def AddOrigin(self, where: CFGNode, source_set: Iterable[Binding]) -> None: ...
  def AssignToNewVariable(self, where: Optional[CFGNode] = ...) -> Variable: ...
  def HasSource(self, binding: Binding) -> bool: ...

class Origin:
  where: CFGNode
  source_sets = list[set[Binding]]

class NodeMetrics:
  incoming_edge_count: int
  outgoing_edge_count: int
  has_condition: bool

class VariableMetrics:
  binding_count: int
  node_ids: list[int]

class QueryStep:
  node: int
  bindings: list[int]
  depth: int

class QueryMetrics:
  nodes_visited: int
  start_node: int
  end_node: int
  initial_binding_count: int
  total_binding_count: int
  shortcircuited: bool
  from_cache: bool
  steps: list[QueryStep]

class CacheMetrics:
  total_size: int
  hits: int
  misses: int

class SolverMetrics:
  query_metrics: list[QueryMetrics]
  cache_metrics: list[CacheMetrics]

class Metrics:
  binding_count: int
  cfg_node_metrics: list[NodeMetrics]
  variable_metrics: list[VariableMetrics]
  solver_metrics: list[SolverMetrics]
