/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package common

import (
	"errors"
	"gen/thrifttest"
	"reflect"
	"testing"
	"thrift"

	"github.com/golang/mock/gomock"
)

type test_unit struct {
	host          string
	port          int64
	domain_socket string
	transport     string
	protocol      string
	ssl           bool
}

var units = []test_unit{
	{"127.0.0.1", 9090, "", "", "binary", false},
	{"127.0.0.1", 9091, "", "", "compact", false},
	{"127.0.0.1", 9092, "", "", "binary", true},
	{"127.0.0.1", 9093, "", "", "compact", true},
}

func TestAllConnection(t *testing.T) {
	certPath = "../../../keys"
	for _, unit := range units {
		t.Logf("%#v", unit)
		doUnit(t, &unit)
	}
}

func doUnit(t *testing.T, unit *test_unit) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	handler := NewMockThriftTest(ctrl)

	processor, serverTransport, transportFactory, protocolFactory, err := GetServerParams(unit.host, unit.port, unit.domain_socket, unit.transport, unit.protocol, unit.ssl, "../../../keys", handler)

	server := thrift.NewTSimpleServer4(processor, serverTransport, transportFactory, protocolFactory)
	if err = server.Listen(); err != nil {
		t.Errorf("Unable to start server", err)
		t.FailNow()
	}
	go server.AcceptLoop()
	defer server.Stop()
	client, err := StartClient(unit.host, unit.port, unit.domain_socket, unit.transport, unit.protocol, unit.ssl)
	if err != nil {
		t.Errorf("Unable to start client", err)
		t.FailNow()
	}
	defer client.Transport.Close()
	callEverythingWithMock(t, client, handler)
}

var rmapmap = map[int32]map[int32]int32{
	-4: map[int32]int32{-4: -4, -3: -3, -2: -2, -1: -1},
	4:  map[int32]int32{4: 4, 3: 3, 2: 2, 1: 1},
}

var xxs = &thrifttest.Xtruct{
	StringThing: "Hello2",
	ByteThing:   42,
	I32Thing:    4242,
	I64Thing:    424242,
}

var xcept = &thrifttest.Xception{ErrorCode: 1001, Message: "some"}

func callEverythingWithMock(t *testing.T, client *thrifttest.ThriftTestClient, handler *MockThriftTest) {
	gomock.InOrder(
		handler.EXPECT().TestVoid(),
		handler.EXPECT().TestString("thing").Return("thing", nil),
		handler.EXPECT().TestBool(true).Return(true, nil),
		handler.EXPECT().TestBool(false).Return(false, nil),
		handler.EXPECT().TestByte(int8(42)).Return(int8(42), nil),
		handler.EXPECT().TestI32(int32(4242)).Return(int32(4242), nil),
		handler.EXPECT().TestI64(int64(424242)).Return(int64(424242), nil),
		// TODO: add TestBinary()
		handler.EXPECT().TestDouble(float64(42.42)).Return(float64(42.42), nil),
		handler.EXPECT().TestStruct(&thrifttest.Xtruct{StringThing: "thing", ByteThing: 42, I32Thing: 4242, I64Thing: 424242}).Return(&thrifttest.Xtruct{StringThing: "thing", ByteThing: 42, I32Thing: 4242, I64Thing: 424242}, nil),
		handler.EXPECT().TestNest(&thrifttest.Xtruct2{StructThing: &thrifttest.Xtruct{StringThing: "thing", ByteThing: 42, I32Thing: 4242, I64Thing: 424242}}).Return(&thrifttest.Xtruct2{StructThing: &thrifttest.Xtruct{StringThing: "thing", ByteThing: 42, I32Thing: 4242, I64Thing: 424242}}, nil),
		handler.EXPECT().TestMap(map[int32]int32{1: 2, 3: 4, 5: 42}).Return(map[int32]int32{1: 2, 3: 4, 5: 42}, nil),
		handler.EXPECT().TestStringMap(map[string]string{"a": "2", "b": "blah", "some": "thing"}).Return(map[string]string{"a": "2", "b": "blah", "some": "thing"}, nil),
		handler.EXPECT().TestSet(map[int32]struct{}{1: struct{}{}, 2: struct{}{}, 42: struct{}{}}).Return(map[int32]struct{}{1: struct{}{}, 2: struct{}{}, 42: struct{}{}}, nil),
		handler.EXPECT().TestList([]int32{1, 2, 42}).Return([]int32{1, 2, 42}, nil),
		handler.EXPECT().TestEnum(thrifttest.Numberz_TWO).Return(thrifttest.Numberz_TWO, nil),
		handler.EXPECT().TestTypedef(thrifttest.UserId(42)).Return(thrifttest.UserId(42), nil),
		handler.EXPECT().TestMapMap(int32(42)).Return(rmapmap, nil),
		// TODO: not testing insanity
		handler.EXPECT().TestMulti(int8(42), int32(4242), int64(424242), map[int16]string{1: "blah", 2: "thing"}, thrifttest.Numberz_EIGHT, thrifttest.UserId(24)).Return(xxs, nil),
		handler.EXPECT().TestException("some").Return(xcept),
		handler.EXPECT().TestException("TException").Return(errors.New("Just random exception")),
		handler.EXPECT().TestMultiException("Xception", "ignoreme").Return(nil, &thrifttest.Xception{ErrorCode: 1001, Message: "This is an Xception"}),
		handler.EXPECT().TestMultiException("Xception2", "ignoreme").Return(nil, &thrifttest.Xception2{ErrorCode: 2002, StructThing: &thrifttest.Xtruct{StringThing: "This is an Xception2"}}),
		handler.EXPECT().TestOneway(int32(2)).Return(nil),
		handler.EXPECT().TestVoid(),
	)
	var err error
	if err = client.TestVoid(); err != nil {
		t.Errorf("Unexpected error in TestVoid() call: ", err)
	}

	thing, err := client.TestString("thing")
	if err != nil {
		t.Errorf("Unexpected error in TestString() call: ", err)
	}
	if thing != "thing" {
		t.Errorf("Unexpected TestString() result, expected 'thing' got '%s' ", thing)
	}

	bl, err := client.TestBool(true)
	if err != nil {
		t.Errorf("Unexpected error in TestBool() call: ", err)
	}
	if !bl {
		t.Errorf("Unexpected TestBool() result expected true, got %f ", bl)
	}
	bl, err = client.TestBool(false)
	if err != nil {
		t.Errorf("Unexpected error in TestBool() call: ", err)
	}
	if bl {
		t.Errorf("Unexpected TestBool() result expected false, got %f ", bl)
	}

	b, err := client.TestByte(42)
	if err != nil {
		t.Errorf("Unexpected error in TestByte() call: ", err)
	}
	if b != 42 {
		t.Errorf("Unexpected TestByte() result expected 42, got %d ", b)
	}

	i32, err := client.TestI32(4242)
	if err != nil {
		t.Errorf("Unexpected error in TestI32() call: ", err)
	}
	if i32 != 4242 {
		t.Errorf("Unexpected TestI32() result expected 4242, got %d ", i32)
	}

	i64, err := client.TestI64(424242)
	if err != nil {
		t.Errorf("Unexpected error in TestI64() call: ", err)
	}
	if i64 != 424242 {
		t.Errorf("Unexpected TestI64() result expected 424242, got %d ", i64)
	}

	d, err := client.TestDouble(42.42)
	if err != nil {
		t.Errorf("Unexpected error in TestDouble() call: ", err)
	}
	if d != 42.42 {
		t.Errorf("Unexpected TestDouble() result expected 42.42, got %f ", d)
	}

	// TODO: add TestBinary() call

	xs := thrifttest.NewXtruct()
	xs.StringThing = "thing"
	xs.ByteThing = 42
	xs.I32Thing = 4242
	xs.I64Thing = 424242
	xsret, err := client.TestStruct(xs)
	if err != nil {
		t.Errorf("Unexpected error in TestStruct() call: ", err)
	}
	if *xs != *xsret {
		t.Errorf("Unexpected TestStruct() result expected %#v, got %#v ", xs, xsret)
	}

	x2 := thrifttest.NewXtruct2()
	x2.StructThing = xs
	x2ret, err := client.TestNest(x2)
	if err != nil {
		t.Errorf("Unexpected error in TestNest() call: ", err)
	}
	if !reflect.DeepEqual(x2, x2ret) {
		t.Errorf("Unexpected TestNest() result expected %#v, got %#v ", x2, x2ret)
	}

	m := map[int32]int32{1: 2, 3: 4, 5: 42}
	mret, err := client.TestMap(m)
	if err != nil {
		t.Errorf("Unexpected error in TestMap() call: ", err)
	}
	if !reflect.DeepEqual(m, mret) {
		t.Errorf("Unexpected TestMap() result expected %#v, got %#v ", m, mret)
	}

	sm := map[string]string{"a": "2", "b": "blah", "some": "thing"}
	smret, err := client.TestStringMap(sm)
	if err != nil {
		t.Errorf("Unexpected error in TestStringMap() call: ", err)
	}
	if !reflect.DeepEqual(sm, smret) {
		t.Errorf("Unexpected TestStringMap() result expected %#v, got %#v ", sm, smret)
	}

	s := map[int32]struct{}{1: struct{}{}, 2: struct{}{}, 42: struct{}{}}
	sret, err := client.TestSet(s)
	if err != nil {
		t.Errorf("Unexpected error in TestSet() call: ", err)
	}
	if !reflect.DeepEqual(s, sret) {
		t.Errorf("Unexpected TestSet() result expected %#v, got %#v ", s, sret)
	}

	l := []int32{1, 2, 42}
	lret, err := client.TestList(l)
	if err != nil {
		t.Errorf("Unexpected error in TestList() call: ", err)
	}
	if !reflect.DeepEqual(l, lret) {
		t.Errorf("Unexpected TestSet() result expected %#v, got %#v ", l, lret)
	}

	eret, err := client.TestEnum(thrifttest.Numberz_TWO)
	if err != nil {
		t.Errorf("Unexpected error in TestEnum() call: ", err)
	}
	if eret != thrifttest.Numberz_TWO {
		t.Errorf("Unexpected TestEnum() result expected %#v, got %#v ", thrifttest.Numberz_TWO, eret)
	}

	tret, err := client.TestTypedef(thrifttest.UserId(42))
	if err != nil {
		t.Errorf("Unexpected error in TestTypedef() call: ", err)
	}
	if tret != thrifttest.UserId(42) {
		t.Errorf("Unexpected TestTypedef() result expected %#v, got %#v ", thrifttest.UserId(42), tret)
	}

	mapmap, err := client.TestMapMap(42)
	if err != nil {
		t.Errorf("Unexpected error in TestMapmap() call: ", err)
	}
	if !reflect.DeepEqual(mapmap, rmapmap) {
		t.Errorf("Unexpected TestMapmap() result expected %#v, got %#v ", rmapmap, mapmap)
	}

	xxsret, err := client.TestMulti(42, 4242, 424242, map[int16]string{1: "blah", 2: "thing"}, thrifttest.Numberz_EIGHT, thrifttest.UserId(24))
	if err != nil {
		t.Errorf("Unexpected error in TestMulti() call: ", err)
	}
	if !reflect.DeepEqual(xxs, xxsret) {
		t.Errorf("Unexpected TestMulti() result expected %#v, got %#v ", xxs, xxsret)
	}

	err = client.TestException("some")
	if err == nil {
		t.Errorf("Expecting exception in TestException() call")
	}
	if !reflect.DeepEqual(err, xcept) {
		t.Errorf("Unexpected TestException() result expected %#v, got %#v ", xcept, err)
	}

	// TODO: connection is being closed on this
	err = client.TestException("TException")
	tex, ok := err.(thrift.TApplicationException)
	if err == nil || !ok || tex.TypeId() != thrift.INTERNAL_ERROR {
		t.Errorf("Unexpected TestException() result expected ApplicationError, got %#v ", err)
	}

	ign, err := client.TestMultiException("Xception", "ignoreme")
	if ign != nil || err == nil {
		t.Errorf("Expecting exception in TestMultiException() call")
	}
	if !reflect.DeepEqual(err, &thrifttest.Xception{ErrorCode: 1001, Message: "This is an Xception"}) {
		t.Errorf("Unexpected TestMultiException() %#v ", err)
	}

	ign, err = client.TestMultiException("Xception2", "ignoreme")
	if ign != nil || err == nil {
		t.Errorf("Expecting exception in TestMultiException() call")
	}
	expecting := &thrifttest.Xception2{ErrorCode: 2002, StructThing: &thrifttest.Xtruct{StringThing: "This is an Xception2"}}

	if !reflect.DeepEqual(err, expecting) {
		t.Errorf("Unexpected TestMultiException() %#v ", err)
	}

	err = client.TestOneway(2)
	if err != nil {
		t.Errorf("Unexpected error in TestOneway() call: ", err)
	}

	//Make sure the connection still alive
	if err = client.TestVoid(); err != nil {
		t.Errorf("Unexpected error in TestVoid() call: ", err)
	}
}
