package com.iot.parking.repository;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import com.iot.parking.model.Slot;

@Repository
public interface SlotRepository extends JpaRepository<Slot, Long> {}
