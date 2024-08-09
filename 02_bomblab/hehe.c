
void main()
{
  int rbp, rax, r12, rbx, rsi, rdi;
  int *arr, *r13, *rsp, *r14;
  r14 = rsp;
  r12 = 0;

  while (r12 < 5)
  {
    rbp = r13; // my input pointer;
    rax = *r13;
    if (rax > 6)
    {
      explode_bomb();
    }
    r12++;

    if (r12 == 6)
    {
      break;
    }

    rbx = r12;
    do
    {
      rax = *(rsp + r12);  // arr[r12]
      if (arr[rbp] == rax) // rbp == 0
      {
        explode();
      }
      rbx++;
    } while (rbx <= 5);

    r13 += 0x4; // increment the pointer to my arr
  }

  rsi = rsp + 0x18;
  
}
