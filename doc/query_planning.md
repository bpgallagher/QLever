#Query Planning


##Strategy:

1. Create a graph from the query.
Each triple corresponds to a node, there is an edge between two nodes iff they share a variable.
Text operations always form cliques (all triples are connected via the context variable).
Turn them into a single nodes with the word-part stored as payload.
This node naturally has an edge to each connected variable.

2. Build a QueryExecutionTree object.
The nodes in the tree are QueryExecutionTrees themselves. Each one has a single "Operation" member.
These can be SCAN, JOIN, SORT, and so on, see the section on Operations below.
Such operations can have a no children (e.g., SCAN, TEXT_NO_FILTER), one child (SORT, TEXT_WITH_FILTER, DISTINCT, FILTER, ORDER_BY) or two children (JOIN, TWO_COLUMN_JOIN). 
Children are always new nodes (i.e. QueryExecutionTree objects) and their results can be cached and/or reused within a query (e.g. a SCAN for type/object/name).
In general, each leaf of the graph will correspond to an operation with no children.

3. Modifiers like ORDER_BY or DISTINCT are applied in the end (topmost in the tree). 
LIMIT and OFFSET aren't applied at all and only considered when creating readable (JSON, std::cout, etc) results.


##Operations:

###Obvious: 
SCAN, JOIN, SORT, DISTINCT, FILTER, ORDER_BY

###Non-obvious:

####TEXT_NO_FILTER: 
A text operation that, for given words (and a set TEXTLIMIT, default = 1), returns a table with 3 or more columns (context id, score, entities...). 
In the most common case, there are three columns and the entity column holds entities that co-occur with the words.
There are more of them when the SPARQL variable for the context is connected to more than one other variable, i.e. after the clique-collapsing described above, the combined nodes has multiple edges.
TEXTLIMIT determines the max rows per combination of entities.
This operation is enough to answer a query like

    SELECT ?x WHERE { ?x <in-context> ?c . ?c <in-context> edible leaves }
    
And with another join (with a list of plants obtained from a scan) it can be used to answer 

    SELECT ?x WHERE { ?x <is-a> <Plant> . ?x <in-context> ?c . ?c <in-context> edible leaves }
    
####TEXT_WITH_FILTER:

A text operation like the one above with exactly one subtree as a child. Particularly useful to answer queries like:

    SELECT ?x WHERE { ?x <is-a> <Computer_scientist> . ?x <in-context> ?c . ?c <in-context> comp* }
    
or 
    
    SELECT ?p ?x WHERE { 
        ?p <is-a> <Person> . 
        ?x <is-a> <Computer_scientist> . 
        ?p <in-context> ?c . 
        ?x <in-context> ?c . 
        ?c <in-context> comp* . 
        FILTER(?p != ?c)
    }
    
The idea is that the list of computer scientists is much smaller than the list of entities that co-occur with some suffix of comp*.
Thus, the subtree to SCAN for computer scientists is used as a filter (hash set) and all contexts that don't contain one of them are discarded right away.
This means that, at no point the list of all entities (with top context witnesses and scores) that occur with comp* is generated.
Usually the above TEXT_NO_FILTER is the better choice, but in some extreme examples (like this one), filtering has a huge benefit.

####TWO_COLUMN_JOIN:

This operation is only relevant for "cyclic" queries, e.g.:

    SELECT ?a1 ?a2 ?m WHERE { 
        ?a1 <Film_performance> ?m . 
        ?a2 <Film_performance> ?m . 
        ?a1 <Spouse_(or_domestic_partner)> 
        ?a2 . 
        FILTER(?a1 != ?a2)
    }
    
A possible execution tree (disregard better and worse trees for now) would be to JOIN the Film_performance relation on the movie 
(and such create all triples of actor actor movie that performed together in that movie, 
and then use the Spouse relation to only keep those pairs of actors.
Filtering a result table by a relation (exactly two columns) is the most basic (and currently always preferred) way of a TWO_COLUMN_JOIN. 
In principle, it is also possible to join with another sub-result with more than two columns:
Again two columns are used to filter results from the other side, but it may be necessary to create cross products if the "filter values" (join columns) have a multiplicity > 1.  

##Building the execution tree from the graph:

We use a dynamic programming approach. _TODO: read more related work and cite something, it's not new and I read it for RDF3X where it isn't new either._

Let `n` be the number of nodes in the graph (i.e. the number of triples in the SPARQL query for queries without text and possible less for queries with text part). 
We then create DP table with `n` rows where the `k`'th row contains all possible QueryExecutionsTrees for sub-queries that contain `k` triples (`k in [1 .. n]`).
We seed the first row with the `n` SCAN (or TEXT_WITHOUT_FILTER) operations pertaining to the nodes.
 
Then we create row after row by trying all possible merges.
The `k`'th row is created by trying to merge all combinations of rows `i` and `j`, s.th. `i + j = k`.
It is possible to merge two sub-trees iff:  
1) They do not already overlap, i.e. no nodes is already covered by both of them  
2) there is an edge between one of their contained nodes in the query graph.

To merge two subtrees, a join is created. 
Any subtree that is not yet sorted on the join column, is sorted by an extra SORT operation.
There are two special cases:  
1) If at least one subtree pertains to a TEXT_WITHOUT_FILTER operation, we create both possible plans: JOIN normally (plus SORT before) and a TEXT_WITH_FILTER operation.<sup>[\[1\]](#textwfilter)</sup>  
2) If they are connected by more than one edge, we have something we called a "cyclic query" already, and create a special TWO_COLUMN_JOIN operation.

Before we return a row, we prune away execution trees that are certain to be inferior to an alternative:
I.e. we only keep the tree with the lowest cost estimate for each key, where a key consists of: the variable by which it is ordered (or unordered) + the set of triples from the original query that are covered + the set of FILTER statements from the original query that are covered.


After each row, we apply all possible FILTER operations.<sup>[\[2\]](#filterfn)</sup> 
For the next round, each remaining candidate is considered in two variants: with all applicable filters applied and with none of the applied (but not all possible sub set of filters).
This allows FILTER operations to be taken at any time of the query. Usually it is better to take them earlier because they can only reduce the number of elements and are usually fast to evaluate, but sometimes it is better for them to be delayed because only then, a TEXT_WITH_FILTER operation can be created (it's only possible if one of the children is a TEXT_WITHOUT_FILTER operation and not if a FILTER is applied to it already).
The exception is the last row where all FILTERS have to be taken.
Finally, the tree with the lowest cost estimate is used.

&nbsp;

<a name="textwfilter">\[1\]</a>: When a TEXT_WITH_FILTER operation is created, one subtree is kept as a child and the TEXT_WITHOUT_FILTER operation is removed / included in the operation.

<a name="filterfn">\[2\]</a>: A FILTER operation can be applied as soon as all it's variables are covered somewhere in the query. This is possible because the number of distinct elements for each variable becomes lower while the query is executed. It is the highest after an initial SCAN and always corresponds to the intersection after each join. That said, multiplicity and total number fo rows may become larger throughout the query - possibly by a lot. 