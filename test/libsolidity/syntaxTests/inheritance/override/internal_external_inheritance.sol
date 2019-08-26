contract A {
    function f(uint[] calldata) external pure {}
}
contract B {
    function f(uint[] memory) internal pure {}
}
contract C is A, B {}
// ----
// TypeError: (126-147): Ambiguous base function f() must be overridden.
