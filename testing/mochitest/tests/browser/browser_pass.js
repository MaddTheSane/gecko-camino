function test() {
  is(true, true, "pass is");
  ok(true, "pass ok");
  isnot(false, true, "pass isnot");
  todo(false, "pass todo");

  var func = is;
  func(true, true, "pass indirect is");
}
