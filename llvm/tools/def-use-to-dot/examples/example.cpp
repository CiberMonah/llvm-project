int update(int *Pointer, int Value) {
  *Pointer = Value + 1;

  int Loaded = *Pointer;

  if (Loaded > 3)
    return Loaded * 2;

  return Loaded - 1;
}

int main() {
  int Value = 3;
  int Result = update(&Value, Value);

  return Result == 8 ? 0 : 1;
}
